param(
    [int]$Port = 18080,
    [string]$BuildDir = (Join-Path $PSScriptRoot "..\build"),
    [string]$SessionId = "smoke-session",
    [string]$ApiKey = "",
    [switch]$ApiKeyFile
)

$ErrorActionPreference = "Stop"

$serviceExe = Join-Path $BuildDir "pamguard_engine_service.exe"
if (-not (Test-Path $serviceExe)) {
    throw "Service executable not found: $serviceExe"
}

$root = Join-Path ([System.IO.Path]::GetTempPath()) ("pamguard-service-smoke-" + [System.Guid]::NewGuid().ToString("N"))
$sessionDir = Join-Path $root "sessions"
$archiveDir = Join-Path $root "archive"
$ingestStatusPath = Join-Path $root "ingest-status.json"
$auditLogPath = Join-Path $root "audit.ndjson"
New-Item -ItemType Directory -Force -Path $sessionDir, $archiveDir | Out-Null

$oldSessionDir = $env:PAMGUARD_SESSION_CONFIG_DIR
$oldArchiveDir = $env:PAMGUARD_RESULT_ARCHIVE_DIR
$oldIngestStatusFile = $env:PAMGUARD_INGEST_STATUS_FILE
$oldMaxSessions = $env:PAMGUARD_MAX_SESSIONS
$oldRequireSessionMetadata = $env:PAMGUARD_REQUIRE_SESSION_METADATA
$oldAuditLogFile = $env:PAMGUARD_AUDIT_LOG_FILE
$oldApiKey = $env:PAMGUARD_API_KEY
$oldApiKeyFile = $env:PAMGUARD_API_KEY_FILE

$process = $null
try {
    $env:PAMGUARD_SESSION_CONFIG_DIR = $sessionDir
    $env:PAMGUARD_RESULT_ARCHIVE_DIR = $archiveDir
    $env:PAMGUARD_MAX_SESSIONS = "4"
    $env:PAMGUARD_REQUIRE_SESSION_METADATA = "1"
    $env:PAMGUARD_AUDIT_LOG_FILE = $auditLogPath
    $ingestStatus = @{
        schemaVersion = 2
        generatedUnixMs = 1782921600000
        health = "healthy"
        workerCount = 1
        statusCounts = @{
            running = 1
            waiting_restart = 0
            not_started = 0
            exited = 0
            stopped = 0
        }
        healthCounts = @{
            healthy = 1
            degraded = 0
            pending = 0
            stopped = 0
        }
        workers = @(
            @{
                sourceId = "smoke-source"
                sessionId = $SessionId
                status = "running"
                health = "healthy"
                pid = 1234
                restarts = 0
                uptimeMs = 1000
                lastObservedUnixMs = 1782921600000
                lastStartUnixMs = 1782921599000
                lastExitUnixMs = $null
                lastExitCode = $null
                nextStartUnixMs = $null
            }
        )
    } | ConvertTo-Json -Depth 10
    [System.IO.File]::WriteAllText($ingestStatusPath, $ingestStatus, (New-Object System.Text.UTF8Encoding($false)))
    $env:PAMGUARD_INGEST_STATUS_FILE = $ingestStatusPath
    if ($ApiKey) {
        if ($ApiKeyFile) {
            $apiKeyPath = Join-Path $root "api-key.txt"
            [System.IO.File]::WriteAllText($apiKeyPath, "$ApiKey`n", (New-Object System.Text.UTF8Encoding($false)))
            Remove-Item Env:\PAMGUARD_API_KEY -ErrorAction SilentlyContinue
            $env:PAMGUARD_API_KEY_FILE = $apiKeyPath
        }
        else {
            $env:PAMGUARD_API_KEY = $ApiKey
            Remove-Item Env:\PAMGUARD_API_KEY_FILE -ErrorAction SilentlyContinue
        }
    }
    else {
        Remove-Item Env:\PAMGUARD_API_KEY -ErrorAction SilentlyContinue
        Remove-Item Env:\PAMGUARD_API_KEY_FILE -ErrorAction SilentlyContinue
    }

    $process = Start-Process -FilePath $serviceExe -ArgumentList "$Port" -PassThru -WindowStyle Hidden
    $base = "http://127.0.0.1:$Port"
    $headers = @{}
    if ($ApiKey) {
        $headers["X-API-Key"] = $ApiKey
    }

    $healthy = $false
    $lastHealth = $null
    for ($i = 0; $i -lt 50; $i++) {
        try {
            $health = Invoke-RestMethod -Method Get -Uri "$base/health"
            $lastHealth = $health
            if ($health.ok) {
                $healthy = $true
                break
            }
        }
        catch {
            Start-Sleep -Milliseconds 100
        }
    }
    if (-not $healthy) {
        throw "Service did not become healthy"
    }
    if ($lastHealth.resultSchemaVersion -ne 21) {
        throw "Health endpoint did not report result schema version 21"
    }
    if (-not $lastHealth.ingestStatusEnabled) {
        throw "Health endpoint did not report enabled ingest status projection"
    }
    if (-not $lastHealth.sessionMetadataRequired) {
        throw "Health endpoint did not report required session metadata"
    }
    if (-not $lastHealth.auditLogEnabled) {
        throw "Health endpoint did not report enabled audit logging"
    }

    $ready = Invoke-RestMethod -Method Get -Uri "$base/ready"
    if (-not $ready.ready) {
        throw "Service was not ready before creating a session"
    }

    $ingestStatusResult = Invoke-RestMethod -Method Get -Uri "$base/ingest/status" -Headers $headers
    if (-not $ingestStatusResult.configured -or -not $ingestStatusResult.exists -or $ingestStatusResult.status.health -ne "healthy" -or $ingestStatusResult.status.workerCount -ne 1) {
        throw "Ingest status endpoint did not return the configured healthy supervisor status"
    }
    $metricsText = [string](Invoke-WebRequest -Method Get -Uri "$base/metrics" -Headers $headers -UseBasicParsing).Content
    if ($metricsText -notmatch "pamguard_ingest_status_configured 1" -or $metricsText -notmatch 'pamguard_ingest_worker_health\{[^}]*source="smoke-source"') {
        throw "Prometheus metrics did not include ingest supervisor status gauges"
    }

    $metadatalessSession = @{
        sessionId = "metadataless-session"
        sampleRateHz = 48000
        channelCount = 1
        fft = @{
            length = 64
            hop = 32
            windowType = "Hann"
            channels = @(0)
        }
    } | ConvertTo-Json -Depth 10
    $metadataRequest = [System.Net.HttpWebRequest]::Create("$base/sessions")
    $metadataRequest.Method = "POST"
    $metadataRequest.ContentType = "application/json"
    foreach ($key in $headers.Keys) {
        $metadataRequest.Headers[$key] = $headers[$key]
    }
    $metadataBytes = [System.Text.Encoding]::UTF8.GetBytes($metadatalessSession)
    $metadataRequest.ContentLength = $metadataBytes.Length
    $metadataStream = $metadataRequest.GetRequestStream()
    try {
        $metadataStream.Write($metadataBytes, 0, $metadataBytes.Length)
    }
    finally {
        $metadataStream.Dispose()
    }
    try {
        $metadataResponse = $metadataRequest.GetResponse()
        $metadataResponse.Dispose()
        throw "Metadata enforcement accepted a session without ownerId/tenantId"
    }
    catch [System.Net.WebException] {
        if ($_.Exception.Response.StatusCode -ne [System.Net.HttpStatusCode]::BadRequest) {
            throw
        }
        $_.Exception.Response.Dispose()
    }

    $session = @{
        sessionId = $SessionId
        sourceId = "smoke-source"
        ownerId = "smoke-owner"
        tenantId = "smoke-tenant"
        sampleRateHz = 48000
        channelCount = 2
        fft = @{
            length = 64
            hop = 32
            windowType = "Hann"
            channels = @(0, 1)
        }
        array = @{
            id = "smoke-array"
            speedOfSoundMps = 1500.0
            hydrophones = @(
                @{ channel = 0; xM = 0.0; yM = 0.0; zM = 0.0; sensitivityDb = 0.0 },
                @{ channel = 1; xM = 1.0; yM = 0.0; zM = 0.0; sensitivityDb = 0.0 }
            )
        }
        click = @{
            enabled = $true
            localisation = $true
            channelBitmap = 3
            triggerBitmap = 3
            thresholdDb = 10.0
            minTriggerChannels = 1
            shortFilter = 0.1
            longFilter = 0.00001
            preSample = 10
            postSample = 12
            minSep = 8
            maxLength = 128
            featuresEnabled = $true
            basicClassifier = @{
                enabled = $true
                standardTypes = @(
                    @{ standard = "beakedWhale"; speciesCode = 1; discard = $false },
                    "porpoise"
                )
            }
            train = @{
                enabled = $true
                maxIciSeconds = 0.5
                minClicks = 3
            }
        }
        whistle = @{
            enabled = $false
            regionEnabled = $false
        }
    } | ConvertTo-Json -Depth 10

    $created = Invoke-RestMethod -Method Post -Uri "$base/sessions" -Headers $headers -ContentType "application/json" -Body $session
    if (-not $created.created) {
        throw "Session create response did not report created=true"
    }
    if ($created.sourceId -ne "smoke-source" -or $created.ownerId -ne "smoke-owner" -or $created.tenantId -ne "smoke-tenant") {
        throw "Session create response did not preserve source/owner/tenant metadata"
    }

    $sessionList = Invoke-RestMethod -Method Get -Uri "$base/sessions" -Headers $headers
    $listedSession = @($sessionList.sessions) | Where-Object { $_.sessionId -eq $SessionId } | Select-Object -First 1
    if ($sessionList.count -lt 1 -or -not $listedSession) {
        throw "Session list did not include the created session"
    }
    if ($listedSession.ownerId -ne "smoke-owner" -or $listedSession.tenantId -ne "smoke-tenant") {
        throw "Session list did not preserve owner/tenant metadata"
    }
    $filteredSessionList = Invoke-RestMethod -Method Get -Uri "$base/sessions?sourceId=smoke-source&ownerId=smoke-owner&tenantId=smoke-tenant" -Headers $headers
    if ($filteredSessionList.count -ne 1 -or $filteredSessionList.sessions[0].sessionId -ne $SessionId -or $filteredSessionList.totalSessions -lt 1) {
        throw "Filtered session list did not return the expected owner/tenant/source session"
    }
    $emptyFilteredSessionList = Invoke-RestMethod -Method Get -Uri "$base/sessions?ownerId=missing-owner" -Headers $headers
    if ($emptyFilteredSessionList.count -ne 0) {
        throw "Filtered session list returned sessions for a missing owner"
    }

    function New-PcmBlock([int]$Frames, [int]$Channels) {
        $bytes = New-Object byte[] ($Frames * $Channels * 4)
        for ($frame = 0; $frame -lt $Frames; $frame++) {
            $click = if ($frame -ge 80 -and $frame -le 86) { if (($frame % 2) -eq 0) { 0.95 } else { -0.95 } } else { 0.0 }
            for ($channel = 0; $channel -lt $Channels; $channel++) {
                $tone = [Math]::Sin($frame * 0.13 + $channel * 0.31) * 0.01
                $scale = if ($channel -eq 0) { 1.0 } else { 0.82 }
                $value = [float]($tone + $click * $scale)
                [BitConverter]::GetBytes($value).CopyTo($bytes, ($frame * $Channels + $channel) * 4)
            }
        }
        return $bytes
    }

    $pcm = New-PcmBlock -Frames 256 -Channels 2
    $pcmPath = Join-Path $root "chunk.f32le"
    [System.IO.File]::WriteAllBytes($pcmPath, $pcm)

    function Post-PcmChunk([string]$Uri, [string]$Path) {
        $bytes = [System.IO.File]::ReadAllBytes($Path)
        $request = [System.Net.HttpWebRequest]::Create($Uri)
        $request.Method = "POST"
        $request.ContentType = "application/octet-stream"
        $request.ContentLength = $bytes.Length
        foreach ($key in $headers.Keys) {
            $request.Headers[$key] = $headers[$key]
        }
        $stream = $request.GetRequestStream()
        try {
            $stream.Write($bytes, 0, $bytes.Length)
        }
        finally {
            $stream.Dispose()
        }
        try {
            $response = $request.GetResponse()
            $reader = New-Object System.IO.StreamReader($response.GetResponseStream())
            try {
                return ($reader.ReadToEnd() | ConvertFrom-Json)
            }
            finally {
                $reader.Dispose()
                $response.Dispose()
            }
        }
        catch [System.Net.WebException] {
            if ($_.Exception.Response) {
                $reader = New-Object System.IO.StreamReader($_.Exception.Response.GetResponseStream())
                $body = $reader.ReadToEnd()
                $reader.Dispose()
                throw $body
            }
            throw
        }
    }

    $first = Post-PcmChunk -Uri "$base/sessions/$SessionId/pcm-f32le?startSample=0" -Path $pcmPath
    if ($first.schemaVersion -ne 21) {
        throw "PCM response did not report result schema version 21"
    }
    if ($first.sampleContinuity -ne "first" -or $first.nextExpectedStartSample -ne 256) {
        throw "First PCM continuity mismatch"
    }
    if ($first.ownerId -ne "smoke-owner" -or $first.tenantId -ne "smoke-tenant") {
        throw "PCM response did not preserve owner/tenant metadata"
    }
    if (@($first.clickLocalisations).Count -lt 1 -or @($first.clickLocalisations[0].delays).Count -lt 1) {
        throw "Multi-channel click localisation delays were not returned by the HTTP service"
    }
    if (@($first.clickBearings).Count -lt 1 -or $first.clickBearings[0].usedPairs -lt 1) {
        throw "Multi-channel click bearing output was not returned by the HTTP service"
    }
    if ($null -ne $first.clickLocalisations[0].lsqBearing) {
        throw "Two-channel session should not produce LSQ bearing output (needs four-plus hydrophones)"
    }
    if ($first.clickLocalisations[0].arrayShape -ne "line") {
        throw "Two separated hydrophones should be reported as a line array, got '$($first.clickLocalisations[0].arrayShape)'"
    }
    if ($first.clickLocalisations[0].bearingLocaliser -ne "pair") {
        throw "A line sub-array should select the pair bearing localiser, got '$($first.clickLocalisations[0].bearingLocaliser)'"
    }
    if ($null -ne $first.clickLocalisations[0].gridBearing) {
        throw "A line sub-array should not produce grid bearing output"
    }
    $smokePairVectors = $first.clickLocalisations[0].delays[0].pairBearingWorldVectors
    if (@($smokePairVectors).Count -ne 2) {
        throw "A pair bearing should carry two world vectors (the left/right pair), got $(@($smokePairVectors).Count)"
    }
    if (-not $smokePairVectors[0].cone -or -not $smokePairVectors[1].cone) {
        throw "Pair bearing world vectors should both be flagged as cones"
    }

    $second = Post-PcmChunk -Uri "$base/sessions/$SessionId/pcm-f32le?startSample=256" -Path $pcmPath
    if ($second.sampleContinuity -ne "contiguous" -or $second.nextExpectedStartSample -ne 512) {
        throw "Second PCM continuity mismatch"
    }

    $third = Post-PcmChunk -Uri "$base/sessions/$SessionId/pcm-f32le?startSample=512" -Path $pcmPath
    if ($third.sampleContinuity -ne "contiguous" -or $third.nextExpectedStartSample -ne 768) {
        throw "Third PCM continuity mismatch"
    }
    $liveTrain = @($third.clickTrains) | Select-Object -First 1
    if (-not $liveTrain -or $liveTrain.durationSamples -le 0 -or $liveTrain.clickRateHz -le 0 -or $liveTrain.maxIciSeconds -lt $liveTrain.minIciSeconds) {
        throw "Live click train response did not include expected timing metrics"
    }
    $liveLinkedClick = @($third.clicks) | Where-Object { $_.relatedTrainIds -and $_.relatedTrainIds.Count -gt 0 } | Select-Object -First 1
    if (-not $liveLinkedClick) {
        throw "Live click response did not include relatedTrainIds after click train creation"
    }

    $status = Invoke-RestMethod -Method Get -Uri "$base/sessions/$SessionId" -Headers $headers
    if (-not $status.runtime -or $status.runtime.createdUnixMs -le 0 -or $status.runtime.lastReceiveUnixMs -le 0) {
        throw "Runtime wall-clock timestamps were not populated"
    }
    if ($status.ownerId -ne "smoke-owner" -or $status.tenantId -ne "smoke-tenant") {
        throw "Session detail did not preserve owner/tenant metadata"
    }
    if (-not $status.status -or $status.status.hasReceivedAudio -ne $true -or $status.status.activityState -ne "audio-received") {
        throw "Operational session status was not populated after PCM ingest"
    }
    if ($status.status.totalDetectorOutputs -lt 1 -or $status.status.sampleTimelineOk -ne $true) {
        throw "Operational session status counters were not populated as expected"
    }
    if (-not $status.array.clickLocalisationReadiness -or
        $status.array.clickLocalisationReadiness.mode -ne "geometry-constrained" -or
        $status.array.clickLocalisationReadiness.geometryComplete -ne $true -or
        @($status.array.clickLocalisationReadiness.missingClickHydrophoneChannels).Count -ne 0) {
        throw "Session status did not report complete click localisation geometry"
    }

    $archive = Invoke-RestMethod -Method Get -Uri "$base/sessions/$SessionId/archive?limit=2" -Headers $headers
    if ($archive.count -ne 2) {
        throw "Archive tail query expected two records"
    }
    $fullArchive = Invoke-RestMethod -Method Get -Uri "$base/sessions/$SessionId/archive?limit=10" -Headers $headers
    if ($fullArchive.count -ne 3) {
        throw "Archive query expected three records"
    }
    if ($fullArchive.records[0].schemaVersion -ne 21) {
        throw "Archived result record did not preserve result schema version 21"
    }
    $filteredArchive = Invoke-RestMethod -Method Get -Uri "$base/sessions/$SessionId/archive?startSampleFrom=256&startSampleTo=256&limit=10" -Headers $headers
    if ($filteredArchive.count -ne 1 -or $filteredArchive.records[0].startSample -ne 256) {
        throw "Archive startSample range filter did not return the expected second record"
    }

    $clickEvents = Invoke-RestMethod -Method Get -Uri "$base/sessions/$SessionId/archive/detections?type=click&limit=10" -Headers $headers
    if ($clickEvents.count -lt 1) {
        throw "Archive detection event query did not return click events"
    }
    if ($clickEvents.indexed -ne $true) {
        throw "Archive detection event query did not use the sidecar index"
    }
    foreach ($event in @($clickEvents.events)) {
        if ($event.type -ne "click") {
            throw "Archive detection event type filter returned a non-click event"
        }
        if ($event.sourceId -ne "smoke-source" -or $event.ownerId -ne "smoke-owner" -or $event.tenantId -ne "smoke-tenant") {
            throw "Archive detection event did not preserve owner/tenant metadata"
        }
    }

    $ownedClickEvents = Invoke-RestMethod -Method Get -Uri "$base/sessions/$SessionId/archive/detections?type=click&sourceId=smoke-source&ownerId=smoke-owner&tenantId=smoke-tenant&limit=10" -Headers $headers
    if ($ownedClickEvents.count -ne $clickEvents.count -or $ownedClickEvents.ownerId -ne "smoke-owner" -or $ownedClickEvents.tenantId -ne "smoke-tenant") {
        throw "Archive detection metadata filters did not return the expected owned click events"
    }
    if ($ownedClickEvents.indexed -ne $false) {
        throw "Archive detection metadata filter should fall back to canonical sidecar scan"
    }
    $missingOwnerEvents = Invoke-RestMethod -Method Get -Uri "$base/sessions/$SessionId/archive/detections?type=click&ownerId=missing-owner&limit=10" -Headers $headers
    if ($missingOwnerEvents.count -ne 0) {
        throw "Archive detection metadata filter returned events for a missing owner"
    }

    $ownedClickSummary = Invoke-RestMethod -Method Get -Uri "$base/sessions/$SessionId/archive/detections/summary?type=click&sourceId=smoke-source&ownerId=smoke-owner&tenantId=smoke-tenant" -Headers $headers
    if ($ownedClickSummary.totalCount -ne $clickEvents.count -or $ownedClickSummary.types.click -ne $clickEvents.count) {
        throw "Archive detection summary did not count owned click events"
    }
    if ($ownedClickSummary.indexedAvailable -ne $true -or $ownedClickSummary.source -ne "event-sidecar") {
        throw "Archive detection summary did not report the expected sidecar/index availability"
    }
    $missingOwnerSummary = Invoke-RestMethod -Method Get -Uri "$base/sessions/$SessionId/archive/detections/summary?type=click&ownerId=missing-owner" -Headers $headers
    if ($missingOwnerSummary.totalCount -ne 0) {
        throw "Archive detection summary returned events for a missing owner"
    }

    $cursorClickEvents = Invoke-RestMethod -Method Get -Uri "$base/sessions/$SessionId/archive/detections?type=click&cursor=1&limit=1" -Headers $headers
    if ($cursorClickEvents.count -ne 1 -or $cursorClickEvents.events[0].startSample -lt 256) {
        throw "Archive detection event cursor pagination did not return the expected second click"
    }

    $rangedClickEvents = Invoke-RestMethod -Method Get -Uri "$base/sessions/$SessionId/archive/detections?type=click&startSampleFrom=256&limit=10" -Headers $headers
    if ($rangedClickEvents.count -lt 1) {
        throw "Archive detection event sample range did not return the expected second-chunk click"
    }
    foreach ($event in @($rangedClickEvents.events)) {
        if ($event.startSample -lt 256) {
            throw "Archive detection event sample range returned an event before startSampleFrom"
        }
    }

    $trackEvents = Invoke-RestMethod -Method Get -Uri "$base/sessions/$SessionId/archive/detections?type=click-track&limit=10" -Headers $headers
    if ($trackEvents.count -lt 1) {
        throw "Archive detection event query did not return click-track events"
    }
    if ($trackEvents.events[0].payload.durationSamples -le 0 -or $trackEvents.events[0].payload.clickRateHz -le 0) {
        throw "Archive click-track event did not include expected timing metrics"
    }
    $trackProbeSample = [UInt64]$trackEvents.events[0].startSample
    if ($trackEvents.events[0].endSample -ne $null -and [UInt64]$trackEvents.events[0].endSample -gt $trackProbeSample) {
        $trackProbeSample = [UInt64]$trackEvents.events[0].endSample
    }
    $overlapTrackEvents = Invoke-RestMethod -Method Get -Uri "$base/sessions/$SessionId/archive/detections?type=click-track&overlapStartSample=$trackProbeSample&overlapEndSample=$trackProbeSample&limit=10" -Headers $headers
    if ($overlapTrackEvents.count -lt 1 -or $overlapTrackEvents.events[0].type -ne "click-track") {
        throw "Archive click-track overlap sample filter did not return the expected ranged event"
    }

    $localisationEvents = Invoke-RestMethod -Method Get -Uri "$base/sessions/$SessionId/archive/detections?type=click-localisation&limit=10" -Headers $headers
    if ($localisationEvents.count -lt 1 -or @($localisationEvents.events[0].payload.delays).Count -lt 1) {
        throw "Archive detection event query did not return click-localisation delay events"
    }
    if ($localisationEvents.events[0].payload.delays[0].delaySeconds -eq $null -or $localisationEvents.events[0].payload.delays[0].pathDifferenceM -eq $null) {
        throw "Archive click-localisation delay event did not include physical delay units"
    }
    if (-not $localisationEvents.events[0].payload.delays[0].geometryConstrained -or $localisationEvents.events[0].payload.delays[0].maxDelaySamples -eq $null -or $localisationEvents.events[0].payload.delays[0].hydrophoneDistanceM -eq $null) {
        throw "Archive click-localisation delay event did not include geometry constraint metadata"
    }
    if ($localisationEvents.events[0].payload.delays[0].audioChannelA -eq $null -or $localisationEvents.events[0].payload.delays[0].audioChannelB -eq $null) {
        throw "Archive click-localisation delay event did not include audio channel metadata"
    }
    if ($localisationEvents.events[0].payload.delays[0].pairBearingRadians -eq $null -or $localisationEvents.events[0].payload.delays[0].pairBearingDegrees -eq $null) {
        throw "Archive click-localisation delay event did not include pair bearing output"
    }

    $bearingEvents = Invoke-RestMethod -Method Get -Uri "$base/sessions/$SessionId/archive/detections?type=click-bearing&limit=10" -Headers $headers
    if ($bearingEvents.count -lt 1 -or $bearingEvents.events[0].payload.usedPairs -lt 1) {
        throw "Archive detection event query did not return click-bearing events"
    }

    $linkedClickEvents = Invoke-RestMethod -Method Get -Uri "$base/sessions/$SessionId/archive/detections?type=click&startSampleFrom=512&limit=10" -Headers $headers
    $linkedClick = @($linkedClickEvents.events) | Where-Object { $_.relatedTrainIds -and $_.relatedTrainIds.Count -gt 0 } | Select-Object -First 1
    if (-not $linkedClick) {
        throw "Archive click event did not include relatedTrainIds after click train creation"
    }

    $csvRequest = [System.Net.HttpWebRequest]::Create("$base/sessions/$SessionId/archive/detections.csv?type=click&ownerId=smoke-owner&limit=10")
    $csvRequest.Method = "GET"
    foreach ($key in $headers.Keys) {
        $csvRequest.Headers[$key] = $headers[$key]
    }
    $csvResponse = $csvRequest.GetResponse()
    $csvReader = New-Object System.IO.StreamReader($csvResponse.GetResponseStream())
    try {
        $csvContent = $csvReader.ReadToEnd()
    }
    finally {
        $csvReader.Dispose()
        $csvResponse.Dispose()
    }
    if ($csvContent -notlike "type,sessionId,sourceId,ownerId,tenantId,startSample*" -or $csvContent -notlike "*click,$SessionId,smoke-source,smoke-owner,smoke-tenant*") {
        throw "Archive detection CSV export did not include expected click rows"
    }

    # Project import end to end: the session JSON produced by
    # PamguardProjectConverter from a real .psfx must be accepted by the live
    # engine as-is (plus the owner/tenant metadata this smoke's service
    # enforces, which a PAMGuard settings file has no notion of).
    $importJsonPath = Join-Path $PSScriptRoot "..\tests\fixtures\project-import\sample-project-session.json"
    if (Test-Path $importJsonPath) {
        $imported = Get-Content $importJsonPath -Raw | ConvertFrom-Json
        $imported | Add-Member -NotePropertyName ownerId -NotePropertyValue "smoke-owner"
        $imported | Add-Member -NotePropertyName tenantId -NotePropertyValue "smoke-tenant"
        $importBody = $imported | ConvertTo-Json -Depth 10
        $importCreate = Invoke-RestMethod -Method Post -Uri "$base/sessions" -Headers $headers -ContentType "application/json" -Body $importBody
        if (-not $importCreate.created) {
            throw "Converted PAMGuard project session was not created"
        }
        $importStatus = Invoke-RestMethod -Method Get -Uri "$base/sessions/$($imported.sessionId)" -Headers $headers
        if ($importStatus.channelCount -ne 4 -or $importStatus.sampleRateHz -ne 96000) {
            throw "Imported session did not round-trip the .psfx acquisition settings"
        }
        if ($importStatus.fft.length -ne 512 -or $importStatus.fft.hop -ne 256) {
            throw "Imported session did not round-trip the .psfx FFT settings"
        }
        $importRemoved = Invoke-RestMethod -Method Delete -Uri "$base/sessions/$($imported.sessionId)" -Headers $headers
        if (-not $importRemoved.removed) {
            throw "Imported session delete did not report removed=true"
        }
    }
    else {
        throw "Project import fixture not found at $importJsonPath"
    }

    $flush = Invoke-RestMethod -Method Post -Uri "$base/sessions/$SessionId/flush" -Headers $headers
    if (-not $flush.flushed) {
        throw "Session flush did not report flushed=true"
    }

    $removed = Invoke-RestMethod -Method Delete -Uri "$base/sessions/$SessionId" -Headers $headers
    if (-not $removed.removed) {
        throw "Session delete did not report removed=true"
    }
    $auditText = [System.IO.File]::ReadAllText($auditLogPath)
    foreach ($expectedAuditEvent in @("session_create_rejected", "session_create", "session_flush", "session_delete")) {
        if ($auditText -notmatch $expectedAuditEvent) {
            throw "Audit log did not include expected event $expectedAuditEvent"
        }
    }

    Write-Host "Service smoke passed: session=$SessionId archiveRecords=$($archive.count) clickEvents=$($clickEvents.count)"
}
finally {
    if ($process -and -not $process.HasExited) {
        Stop-Process -Id $process.Id -Force
    }
    $env:PAMGUARD_SESSION_CONFIG_DIR = $oldSessionDir
    $env:PAMGUARD_RESULT_ARCHIVE_DIR = $oldArchiveDir
    if ($oldIngestStatusFile) {
        $env:PAMGUARD_INGEST_STATUS_FILE = $oldIngestStatusFile
    }
    else {
        Remove-Item Env:\PAMGUARD_INGEST_STATUS_FILE -ErrorAction SilentlyContinue
    }
    if ($oldRequireSessionMetadata) {
        $env:PAMGUARD_REQUIRE_SESSION_METADATA = $oldRequireSessionMetadata
    }
    else {
        Remove-Item Env:\PAMGUARD_REQUIRE_SESSION_METADATA -ErrorAction SilentlyContinue
    }
    if ($oldAuditLogFile) {
        $env:PAMGUARD_AUDIT_LOG_FILE = $oldAuditLogFile
    }
    else {
        Remove-Item Env:\PAMGUARD_AUDIT_LOG_FILE -ErrorAction SilentlyContinue
    }
    $env:PAMGUARD_MAX_SESSIONS = $oldMaxSessions
    if ($oldApiKey) {
        $env:PAMGUARD_API_KEY = $oldApiKey
    }
    else {
        Remove-Item Env:\PAMGUARD_API_KEY -ErrorAction SilentlyContinue
    }
    if ($oldApiKeyFile) {
        $env:PAMGUARD_API_KEY_FILE = $oldApiKeyFile
    }
    else {
        Remove-Item Env:\PAMGUARD_API_KEY_FILE -ErrorAction SilentlyContinue
    }
    Remove-Item -Recurse -Force -Path $root -ErrorAction SilentlyContinue
}
