$repoRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$root = Join-Path $repoRoot 'build\RelWithDebInfo'
$configRoot = Join-Path $repoRoot 'config'
$modelConfigRoot = Join-Path $configRoot 'model_config'
$testDataRoot = Join-Path $configRoot 'test_data'

function Get-RequiredFile {
    param(
        [string]$Path,
        [string]$Label
    )

    if (!(Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "Missing ${Label}: $Path"
    }

    return (Resolve-Path -LiteralPath $Path).Path
}

function Get-FirstFile {
    param(
        [string]$Directory,
        [string]$Filter,
        [ScriptBlock]$Predicate = $null
    )

    if (!(Test-Path -LiteralPath $Directory -PathType Container)) {
        return $null
    }

    $items = Get-ChildItem -LiteralPath $Directory -File -Filter $Filter | Sort-Object Name
    if ($null -ne $Predicate) {
        $items = $items | Where-Object $Predicate
    }

    if ($items.Count -gt 0) {
        return $items[0].FullName
    }

    return $null
}

function Resolve-EnginePath {
    $enginePath = Get-FirstFile -Directory $modelConfigRoot -Filter '*.engine'
    if ([string]::IsNullOrWhiteSpace($enginePath)) {
        $enginePath = Get-FirstFile -Directory $root -Filter '*.engine'
    }
    if ([string]::IsNullOrWhiteSpace($enginePath)) {
        throw "Engine file not found. Please place a .engine file under config\model_config or build\RelWithDebInfo."
    }

    return (Resolve-Path -LiteralPath $enginePath).Path
}

function Resolve-SourceRequest {
    $requestPath = Get-FirstFile -Directory $configRoot -Filter '*.json' -Predicate { $_.Name -notlike '*_result.json' }
    if ([string]::IsNullOrWhiteSpace($requestPath)) {
        throw "Request JSON not found. Please place an input request JSON file under config."
    }

    return (Resolve-Path -LiteralPath $requestPath).Path
}

function Resolve-RelativeAssetSource {
    param(
        [string]$SourceRequestPath,
        [string]$RelativePath
    )

    $normalizedRelativePath = $RelativePath -replace '/', '\\'
    $sourceRequestDir = Split-Path -Parent $SourceRequestPath
    $candidates = @(
        (Join-Path $sourceRequestDir $normalizedRelativePath),
        (Join-Path $testDataRoot $normalizedRelativePath),
        (Join-Path $testDataRoot ([System.IO.Path]::GetFileName($normalizedRelativePath)))
    )

    foreach ($candidate in ($candidates | Select-Object -Unique)) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }

    return $null
}

function Get-RunningEnd2EndProcesses {
    param(
        [string]$ExecutablePath
    )

    return @(Get-Process end2end -ErrorAction SilentlyContinue | Where-Object {
        $processPath = $_.Path
        ![string]::IsNullOrWhiteSpace($processPath) -and $processPath.Equals($ExecutablePath, [System.StringComparison]::OrdinalIgnoreCase)
    })
}

function Get-UdpPortConflicts {
    param(
        [int[]]$Ports
    )

    if (!(Get-Command Get-NetUDPEndpoint -ErrorAction SilentlyContinue)) {
        return @()
    }

    $conflicts = @()
    foreach ($endpoint in (Get-NetUDPEndpoint -ErrorAction SilentlyContinue | Where-Object { $_.LocalPort -in $Ports })) {
        $ownerProcess = Get-Process -Id $endpoint.OwningProcess -ErrorAction SilentlyContinue
        $ownerName = if ($null -ne $ownerProcess) { $ownerProcess.ProcessName } else { 'unknown' }
        $conflicts += [pscustomobject]@{
            LocalAddress = $endpoint.LocalAddress
            LocalPort = $endpoint.LocalPort
            OwningProcess = $endpoint.OwningProcess
            ProcessName = $ownerName
        }
    }

    return $conflicts
}

function Assert-CleanStart {
    param(
        [string]$ExecutablePath,
        [int[]]$Ports
    )

    $messages = @()
    $runningProcesses = Get-RunningEnd2EndProcesses -ExecutablePath $ExecutablePath
    if ($runningProcesses.Count -gt 0) {
        $messages += "Existing end2end.exe processes detected:`n$((($runningProcesses | Sort-Object Id | ForEach-Object { "PID=$($_.Id) Path=$($_.Path)" }) -join "`n"))"
    }

    $portConflicts = @(Get-UdpPortConflicts -Ports $Ports)
    if ($portConflicts.Count -gt 0) {
        $messages += "Test UDP ports are already in use:`n$((($portConflicts | Sort-Object LocalPort, OwningProcess | ForEach-Object { "Port $($_.LocalPort) <- PID=$($_.OwningProcess) ($($_.ProcessName)) Address=$($_.LocalAddress)" }) -join "`n"))"
    }

    if ($messages.Count -gt 0) {
        throw (($messages -join "`n`n") + "`n`nPlease close the old serve/worker windows or stop the processes using these ports, then try again.")
    }
}

$exe = Get-RequiredFile -Path (Join-Path $root 'end2end.exe') -Label 'app'
$client = Get-RequiredFile -Path (Join-Path $root 'udp_client_demo.exe') -Label 'client'
$engine = Resolve-EnginePath
$sourceRequest = Resolve-SourceRequest

$queue = Join-Path $root 'queue'
$shared = Join-Path $root 'shared'
$requestsInbox = Join-Path $shared 'requests\inbox'
$resultsOutbox = Join-Path $shared 'results\outbox'

$listenPort = 9000
$replyPort = 9001

$runId = Get-Date -Format 'yyyyMMdd_HHmmss'
$taskId = "local-$runId"
$requestRelPath = "requests/inbox/$taskId.json"
$resultRelPath = "results/outbox/${taskId}_result.json"
$dstReq = Join-Path $shared ($requestRelPath -replace '/', '\\')
$dstRes = Join-Path $shared ($resultRelPath -replace '/', '\\')

Assert-CleanStart -ExecutablePath $exe -Ports @($listenPort, $replyPort)

New-Item -ItemType Directory -Force -Path $queue, $requestsInbox, $resultsOutbox | Out-Null
Copy-Item -LiteralPath $sourceRequest -Destination $dstReq -Force

try {
    $requestObject = Get-Content -LiteralPath $dstReq -Raw | ConvertFrom-Json
    $imagePathProperty = $requestObject.PSObject.Properties['imagePath']
    if ($null -ne $imagePathProperty) {
        $imagePath = [string]$imagePathProperty.Value
        if (![string]::IsNullOrWhiteSpace($imagePath) -and ![System.IO.Path]::IsPathRooted($imagePath)) {
            $resolvedAsset = Resolve-RelativeAssetSource -SourceRequestPath $sourceRequest -RelativePath $imagePath
            if (![string]::IsNullOrWhiteSpace($resolvedAsset)) {
                $dstImage = Join-Path (Split-Path -Parent $dstReq) ($imagePath -replace '/', '\\')
                $dstImageParent = Split-Path -Parent $dstImage
                if (![string]::IsNullOrWhiteSpace($dstImageParent)) {
                    New-Item -ItemType Directory -Force -Path $dstImageParent | Out-Null
                }
                Copy-Item -LiteralPath $resolvedAsset -Destination $dstImage -Force
            }
        }
    }
}
catch {
    Write-Warning "Failed to parse the test request JSON. Keeping the original request file. Error: $($_.Exception.Message)"
}

Write-Host "Engine: $engine"
Write-Host "Request: $sourceRequest"
Write-Host "Shared request: $dstReq"
Write-Host "Result file: $dstRes"

Start-Process powershell -ArgumentList @(
    '-NoExit',
    '-Command',
    "& `"$exe`" --serve $listenPort `"$queue`" `"$shared`""
)

Start-Sleep -Seconds 2

Start-Process powershell -ArgumentList @(
    '-NoExit',
    '-Command',
    "& `"$exe`" --worker `"$engine`" `"$queue`" `"$shared`""
)

Start-Sleep -Seconds 2

& $client 127.0.0.1 $listenPort 127.0.0.1 $replyPort $taskId $requestRelPath $resultRelPath

$deadline = (Get-Date).AddSeconds(30)
while ((Get-Date) -lt $deadline) {
    if (Test-Path -LiteralPath $dstRes -PathType Leaf) {
        Write-Host "Result file detected: $dstRes"
        break
    }
    Start-Sleep -Milliseconds 500
}

if (!(Test-Path -LiteralPath $dstRes -PathType Leaf)) {
    Write-Warning "No result file detected within 30 seconds. Please check the serve/worker windows."
}