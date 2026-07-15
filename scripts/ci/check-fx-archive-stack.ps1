[CmdletBinding()]
param(
    [Parameter(Mandatory, Position = 0)]
    [Alias('Path')]
    [string[]]$InputPath,

    [ValidateRange(1, 2147483647)]
    [long]$FxSaveLimitBytes = 4096,

    [ValidateRange(1, 2147483647)]
    [long]$FxRestoreLimitBytes = 8192,

    [ValidateRange(1, 2147483647)]
    [long]$OtherFunctionLimitBytes = 4096,

    [ValidateRange(1, 2147483647)]
    [long]$AbsoluteLimitBytes = 16384,

    [ValidateNotNullOrEmpty()]
    [string]$TranslationUnit = 'fx_archive.cpp'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

# C6262 only emits when the configured /analyze:stacksize threshold is
# exceeded. The caller must therefore analyze this translation unit with a
# deliberately low threshold (normally 1) so a missing measurement cannot be
# mistaken for a small frame. XML /analyze logs are preferred because ordinary
# compiler diagnostics do not consistently include the function name.

function Throw-GateFailure {
    param([Parameter(Mandatory)][string]$Message)
    throw "FX archive stack gate: $Message"
}

function Test-C6262Code {
    param([AllowEmptyString()][string]$Value)
    return $Value.Trim() -match '^(?i:C)?6262$'
}

function ConvertTo-FrameBytes {
    param(
        [Parameter(Mandatory)][string]$Value,
        [Parameter(Mandatory)][string]$Context
    )

    $trimmed = $Value.Trim()
    if ($trimmed -notmatch '^(?:[0-9]+|[0-9]{1,3}(?:[, ][0-9]{3})+)$') {
        Throw-GateFailure "invalid frame-byte value '$Value' in $Context"
    }

    $digits = $trimmed.Replace(',', '').Replace(' ', '')
    $parsed = [long]0
    if (-not [long]::TryParse(
            $digits,
            [Globalization.NumberStyles]::None,
            [Globalization.CultureInfo]::InvariantCulture,
            [ref]$parsed)) {
        Throw-GateFailure "frame-byte value '$Value' overflows Int64 in $Context"
    }
    return $parsed
}

function Resolve-StackReportFiles {
    param([Parameter(Mandatory)][string[]]$Specs)

    $files = New-Object 'System.Collections.Generic.List[System.IO.FileInfo]'
    $seen = New-Object 'System.Collections.Generic.HashSet[string]' `
        ([StringComparer]::OrdinalIgnoreCase)

    foreach ($spec in $Specs) {
        if ([string]::IsNullOrWhiteSpace($spec)) {
            Throw-GateFailure 'an input path or glob is empty'
        }

        $matches = @()
        if (Test-Path -LiteralPath $spec) {
            $matches = @(Get-Item -LiteralPath $spec)
        }
        else {
            try {
                $matches = @(Get-ChildItem -Path $spec -File)
            }
            catch {
                Throw-GateFailure "cannot resolve input '$spec': $($_.Exception.Message)"
            }
        }

        if ($matches.Count -eq 0) {
            Throw-GateFailure "input '$spec' did not match any files"
        }

        foreach ($match in $matches) {
            if ($match.PSProvider.Name -ne 'FileSystem' -or
                $match -isnot [IO.FileInfo]) {
                Throw-GateFailure "input '$spec' resolved to a non-file-system item"
            }
            if (-not $match.Exists) {
                Throw-GateFailure "input file disappeared while resolving '$spec'"
            }
            if ($seen.Add($match.FullName)) {
                $files.Add($match)
            }
        }
    }

    if ($files.Count -eq 0) {
        Throw-GateFailure 'no analyzer reports were resolved'
    }
    return $files.ToArray()
}

function Get-XmlFieldValues {
    param(
        [Parameter(Mandatory)][System.Xml.XmlElement]$Node,
        [Parameter(Mandatory)][string[]]$Names,
        [switch]$IncludeDescendants
    )

    $values = New-Object 'System.Collections.Generic.List[string]'
    $seen = New-Object 'System.Collections.Generic.HashSet[string]' `
        ([StringComparer]::Ordinal)

    foreach ($attribute in $Node.Attributes) {
        if ($Names -contains $attribute.LocalName) {
            $value = $attribute.Value.Trim()
            if ($value.Length -gt 0 -and $seen.Add($value)) {
                $values.Add($value)
            }
        }
    }

    foreach ($child in $Node.ChildNodes) {
        if ($child -is [System.Xml.XmlElement] -and
            $Names -contains $child.LocalName) {
            $value = $child.InnerText.Trim()
            if ($value.Length -gt 0 -and $seen.Add($value)) {
                $values.Add($value)
            }
        }
    }

    if ($IncludeDescendants) {
        foreach ($descendant in $Node.SelectNodes('.//*')) {
            if ($Names -contains $descendant.LocalName) {
                $value = $descendant.InnerText.Trim()
                if ($value.Length -gt 0 -and $seen.Add($value)) {
                    $values.Add($value)
                }
            }
        }
    }

    return $values.ToArray()
}

function Test-TargetSource {
    param(
        [Parameter(Mandatory)][string[]]$Values,
        [Parameter(Mandatory)][string]$FileName
    )

    $escaped = [regex]::Escape($FileName)
    foreach ($value in $Values) {
        if ($value -match "(?i)(?:^|[\\/])$escaped(?:$|[\s:(])" -or
            $value -match "(?i)^$escaped$") {
            return $true
        }
    }
    return $false
}

function Get-FunctionIdentity {
    param(
        [Parameter(Mandatory)][string]$RawFunction,
        [Parameter(Mandatory)][string]$Context
    )

    $value = ($RawFunction.Trim() -replace '\s+', ' ')
    if ($value -match '(?i)(?<![A-Za-z0-9_])FX_Save(?![A-Za-z0-9_])') {
        return [pscustomobject]@{
            Identity = 'FX_Save'
            Kind = 'Save'
        }
    }
    if ($value -match '(?i)(?<![A-Za-z0-9_])FX_Restore(?![A-Za-z0-9_])') {
        return [pscustomobject]@{
            Identity = 'FX_Restore'
            Kind = 'Restore'
        }
    }

    $callMatches = [regex]::Matches(
        $value,
        '(?<name>(?:[A-Za-z_~][A-Za-z0-9_~]*::)*[A-Za-z_~][A-Za-z0-9_~]*)\s*\(')
    if ($callMatches.Count -gt 0) {
        return [pscustomobject]@{
            Identity = $callMatches[0].Groups['name'].Value
            Kind = 'Other'
        }
    }

    if ($value -match '(?<name>(?:[A-Za-z_~][A-Za-z0-9_~]*::)*[A-Za-z_~][A-Za-z0-9_~]*)$') {
        return [pscustomobject]@{
            Identity = $Matches['name']
            Kind = 'Other'
        }
    }

    if ($value.Length -eq 0) {
        Throw-GateFailure "empty function name in $Context"
    }
    return [pscustomobject]@{
        Identity = $value
        Kind = 'Other'
    }
}

function Resolve-FunctionValues {
    param(
        [Parameter(Mandatory)][AllowEmptyCollection()][string[]]$Values,
        [Parameter(Mandatory)][string]$Context
    )

    if ($Values.Count -eq 0) {
        Throw-GateFailure "C6262 measurement has no function attribution in $Context; use an XML /analyze log"
    }

    $resolved = @{}
    foreach ($value in $Values) {
        $function = Get-FunctionIdentity -RawFunction $value -Context $Context
        $resolved[$function.Identity] = $function
    }
    if ($resolved.Count -ne 1) {
        $identities = @($resolved.Keys | Sort-Object) -join ', '
        Throw-GateFailure "ambiguous C6262 function attribution ($identities) in $Context"
    }
    return @($resolved.Values)[0]
}

function Get-FrameValuesFromText {
    param(
        [Parameter(Mandatory)][string]$Text,
        [Parameter(Mandatory)][string]$Context
    )

    $pattern = '(?i)\bFunction\s+uses\s+[''"]?\s*' +
        '(?<bytes>(?:[0-9]+|[0-9]{1,3}(?:[, ][0-9]{3})+))' +
        '\s*[''"]?\s+bytes?\s+of\s+stack\b'
    $values = @{}
    foreach ($match in [regex]::Matches($Text, $pattern)) {
        $bytes = ConvertTo-FrameBytes `
            -Value $match.Groups['bytes'].Value -Context $Context
        $values[[string]$bytes] = $bytes
    }
    return @($values.Values)
}

function Get-XmlFrameValues {
    param(
        [Parameter(Mandatory)][System.Xml.XmlElement]$Node,
        [Parameter(Mandatory)][string]$Context
    )

    $values = @{}
    $messageValues = @(Get-XmlFieldValues -Node $Node -Names @(
            'DESCRIPTION', 'MESSAGE', 'MESSAGETEXT', 'TEXT'
        ) -IncludeDescendants)
    if ($messageValues.Count -eq 0) {
        $messageValues = @($Node.InnerText)
    }
    foreach ($messageValue in $messageValues) {
        foreach ($bytes in @(Get-FrameValuesFromText `
                -Text $messageValue -Context $Context)) {
            $values[[string]$bytes] = $bytes
        }
    }

    $fieldNames = @(
        'STACKUSAGE', 'STACKBYTES', 'FRAMESIZE', 'FRAMEBYTES',
        'BYTESOFSTACK', 'ACTUALSTACKSIZE', 'STACKSIZE'
    )
    foreach ($value in @(Get-XmlFieldValues `
            -Node $Node -Names $fieldNames -IncludeDescendants)) {
        if ($value -notmatch '^\s*(?<bytes>(?:[0-9]+|[0-9]{1,3}(?:[, ][0-9]{3})+))\s*(?:bytes?)?\s*$') {
            Throw-GateFailure "invalid explicit stack-size field '$value' in $Context"
        }
        $bytes = ConvertTo-FrameBytes `
            -Value $Matches['bytes'] -Context $Context
        $values[[string]$bytes] = $bytes
    }

    return @($values.Values)
}

function Add-UniqueXmlCandidate {
    param(
        [Parameter(Mandatory)]
        [AllowEmptyCollection()]
        [System.Collections.Generic.List[System.Xml.XmlElement]]$Candidates,
        [Parameter(Mandatory)][System.Xml.XmlElement]$Candidate
    )

    foreach ($existing in $Candidates) {
        if ([object]::ReferenceEquals($existing, $Candidate)) {
            return
        }
    }
    $Candidates.Add($Candidate)
}

function Read-XmlMeasurements {
    param(
        [Parameter(Mandatory)][xml]$Document,
        [Parameter(Mandatory)][string]$ReportPath,
        [Parameter(Mandatory)][string]$TargetFile
    )

    $codeFieldNames = @(
        'DEFECTCODE', 'CODE', 'RULEID', 'RULE', 'CHECKID', 'WARNINGCODE'
    )
    $candidates = New-Object `
        'System.Collections.Generic.List[System.Xml.XmlElement]'

    foreach ($element in $Document.SelectNodes('//*')) {
        foreach ($attribute in $element.Attributes) {
            if ($codeFieldNames -contains $attribute.LocalName -and
                (Test-C6262Code -Value $attribute.Value)) {
                Add-UniqueXmlCandidate -Candidates $candidates -Candidate $element
            }
        }

        if ($codeFieldNames -contains $element.LocalName -and
            (Test-C6262Code -Value $element.InnerText)) {
            if ($element.ParentNode -isnot [System.Xml.XmlElement]) {
                Throw-GateFailure "C6262 code has no diagnostic container in '$ReportPath'"
            }
            Add-UniqueXmlCandidate `
                -Candidates $candidates -Candidate $element.ParentNode
        }
    }

    $measurements = New-Object 'System.Collections.Generic.List[object]'
    $candidateNumber = 0
    foreach ($candidate in $candidates) {
        $candidateNumber++
        $context = "XML diagnostic $candidateNumber in '$ReportPath'"
        $sourceValues = @(Get-XmlFieldValues -Node $candidate -Names @(
                'FILENAME', 'FILE', 'SOURCEFILE', 'FULLPATH', 'FILEPATH',
                'SOURCE', 'URI'
            ) -IncludeDescendants)
        if ($sourceValues.Count -eq 0) {
            Throw-GateFailure "C6262 diagnostic has no source-file attribution in $context"
        }
        if (-not (Test-TargetSource `
                -Values $sourceValues -FileName $TargetFile)) {
            continue
        }

        $functionValues = @(Get-XmlFieldValues -Node $candidate -Names @(
                'FUNCTION', 'FUNCTIONNAME', 'METHOD', 'SYMBOL', 'PROCEDURE'
            ))
        if ($functionValues.Count -eq 0) {
            $functionValues = @(Get-XmlFieldValues -Node $candidate -Names @(
                    'FUNCTION', 'FUNCTIONNAME', 'METHOD', 'SYMBOL', 'PROCEDURE'
                ) -IncludeDescendants)
        }
        $function = Resolve-FunctionValues `
            -Values $functionValues -Context $context

        $frameValues = @(Get-XmlFrameValues `
            -Node $candidate -Context $context)
        if ($frameValues.Count -eq 0) {
            Throw-GateFailure "C6262 diagnostic has no measured frame size in $context"
        }
        if ($frameValues.Count -ne 1) {
            $frames = @($frameValues | Sort-Object) -join ', '
            Throw-GateFailure "ambiguous C6262 frame sizes ($frames) in $context"
        }

        $measurements.Add([pscustomobject]@{
                Function = $function.Identity
                Kind = $function.Kind
                Bytes = [long]$frameValues[0]
                Report = $ReportPath
                Context = $context
            })
    }
    return $measurements.ToArray()
}

function Get-TextDiagnosticSource {
    param([Parameter(Mandatory)][string]$Line)

    $msvcPattern = '(?i)(?<source>.+?)\((?<line>[0-9]+)' +
        '(?:\s*,\s*[0-9]+)?\)\s*:\s*(?:warning|error)\s+C6262\b'
    if ($Line -match $msvcPattern) {
        return $Matches['source'].Trim()
    }

    $colonPattern = '(?i)(?<source>.+?):[0-9]+(?::[0-9]+)?' +
        '\s*:\s*(?:warning|error)\s+C6262\b'
    if ($Line -match $colonPattern) {
        return $Matches['source'].Trim()
    }
    return $null
}

function Get-TextFunctionValues {
    param(
        [Parameter(Mandatory)]
        [AllowEmptyCollection()]
        [AllowEmptyString()]
        [string[]]$Lines,
        [Parameter(Mandatory)][int]$Index
    )

    $values = New-Object 'System.Collections.Generic.List[string]'
    $seen = New-Object 'System.Collections.Generic.HashSet[string]' `
        ([StringComparer]::Ordinal)
    $line = $Lines[$Index]
    $inlinePatterns = @(
        '(?i)\[(?:function(?:\s+name)?|symbol)\s*[:=]\s*[''"]?(?<function>[^\]''"]+)',
        '(?i)\b(?:in|while\s+compiling)\s+(?:the\s+)?function\s+[''"](?<function>[^''"]+)[''"]',
        '(?i)\bfunction\s+[''"](?<function>[^''"]+)[''"]'
    )
    foreach ($pattern in $inlinePatterns) {
        foreach ($match in [regex]::Matches($line, $pattern)) {
            $value = $match.Groups['function'].Value.Trim()
            if ($value.Length -gt 0 -and $seen.Add($value)) {
                $values.Add($value)
            }
        }
    }
    if ($values.Count -gt 0) {
        return $values.ToArray()
    }

    $first = [Math]::Max(0, $Index - 2)
    $last = [Math]::Min($Lines.Count - 1, $Index + 2)
    $metadataPattern = '^\s*(?:function(?:\s+name)?|symbol)' +
        '\s*[:=]\s*[''"]?(?<function>.+?)[''"]?\s*$'
    $compilingPattern = '(?i)\b(?:in|while\s+compiling)\s+' +
        '(?:the\s+)?function\s+[''"](?<function>[^''"]+)[''"]'
    for ($neighbor = $first; $neighbor -le $last; $neighbor++) {
        if ($neighbor -eq $Index -or $Lines[$neighbor] -match '(?i)C6262') {
            continue
        }
        foreach ($pattern in @($metadataPattern, $compilingPattern)) {
            foreach ($match in [regex]::Matches($Lines[$neighbor], $pattern)) {
                $value = $match.Groups['function'].Value.Trim()
                if ($value.Length -gt 0 -and $seen.Add($value)) {
                    $values.Add($value)
                }
            }
        }
    }
    return $values.ToArray()
}

function Read-TextMeasurements {
    param(
        [Parameter(Mandatory)][string]$Content,
        [Parameter(Mandatory)][string]$ReportPath,
        [Parameter(Mandatory)][string]$TargetFile
    )

    $lines = [regex]::Split($Content, "\r\n|\n|\r")
    $measurements = New-Object 'System.Collections.Generic.List[object]'
    for ($index = 0; $index -lt $lines.Count; $index++) {
        $line = $lines[$index]
        if ($line -notmatch '(?i)C6262') {
            continue
        }

        $context = "text diagnostic on line $($index + 1) of '$ReportPath'"
        $source = Get-TextDiagnosticSource -Line $line
        if ([string]::IsNullOrWhiteSpace($source)) {
            Throw-GateFailure "C6262 diagnostic has no recognizable source location in $context"
        }
        if (-not (Test-TargetSource `
                -Values @($source) -FileName $TargetFile)) {
            continue
        }

        $frameValues = @(Get-FrameValuesFromText `
            -Text $line -Context $context)
        if ($frameValues.Count -eq 0) {
            Throw-GateFailure "C6262 diagnostic has no measured frame size in $context"
        }
        if ($frameValues.Count -ne 1) {
            $frames = @($frameValues | Sort-Object) -join ', '
            Throw-GateFailure "ambiguous C6262 frame sizes ($frames) in $context"
        }

        $functionValues = @(Get-TextFunctionValues `
            -Lines $lines -Index $index)
        $function = Resolve-FunctionValues `
            -Values $functionValues -Context $context

        $measurements.Add([pscustomobject]@{
                Function = $function.Identity
                Kind = $function.Kind
                Bytes = [long]$frameValues[0]
                Report = $ReportPath
                Context = $context
            })
    }
    return $measurements.ToArray()
}

function Get-FunctionLimit {
    param([Parameter(Mandatory)][string]$Kind)

    switch ($Kind) {
        'Save' { return $FxSaveLimitBytes }
        'Restore' { return $FxRestoreLimitBytes }
        default { return $OtherFunctionLimitBytes }
    }
}

function Invoke-FxArchiveStackGate {
    $reportFiles = @(Resolve-StackReportFiles -Specs $InputPath)
    $measurements = New-Object 'System.Collections.Generic.List[object]'
    foreach ($reportFile in $reportFiles) {
        if ($reportFile.Length -eq 0) {
            Throw-GateFailure "analyzer report '$($reportFile.FullName)' is empty"
        }

        $content = [IO.File]::ReadAllText($reportFile.FullName)
        if ([string]::IsNullOrWhiteSpace($content)) {
            Throw-GateFailure "analyzer report '$($reportFile.FullName)' contains only whitespace"
        }

        $trimmed = $content.TrimStart()
        $isXml = $reportFile.Extension -eq '.xml' -or $trimmed.StartsWith('<')
        if ($isXml) {
            try {
                $document = New-Object System.Xml.XmlDocument
                $document.PreserveWhitespace = $true
                $document.LoadXml($content)
            }
            catch {
                Throw-GateFailure "analyzer XML '$($reportFile.FullName)' is malformed: $($_.Exception.Message)"
            }
            foreach ($measurement in @(Read-XmlMeasurements `
                    -Document $document `
                    -ReportPath $reportFile.FullName `
                    -TargetFile $TranslationUnit)) {
                $measurements.Add($measurement)
            }
        }
        else {
            foreach ($measurement in @(Read-TextMeasurements `
                    -Content $content `
                    -ReportPath $reportFile.FullName `
                    -TargetFile $TranslationUnit)) {
                $measurements.Add($measurement)
            }
        }
    }

    if ($measurements.Count -eq 0) {
        Throw-GateFailure "no C6262 measurements for '$TranslationUnit' were found; ensure /analyze:stacksize is set low enough to report every gated function"
    }

    $maximumByFunction = @{}
    foreach ($measurement in $measurements) {
        if (-not $maximumByFunction.ContainsKey($measurement.Function) -or
            $measurement.Bytes -gt $maximumByFunction[$measurement.Function].Bytes) {
            $maximumByFunction[$measurement.Function] = $measurement
        }
    }

    if (-not $maximumByFunction.ContainsKey('FX_Save')) {
        Throw-GateFailure 'the analyzer reports contain no FX_Save measurement'
    }
    if (-not $maximumByFunction.ContainsKey('FX_Restore')) {
        Throw-GateFailure 'the analyzer reports contain no FX_Restore measurement'
    }

    Write-Host 'FX archive MSVC stack measurements:'
    Write-Host ("  limits: FX_Save={0}, FX_Restore={1}, other={2}, absolute={3} bytes" -f `
        $FxSaveLimitBytes, $FxRestoreLimitBytes,
        $OtherFunctionLimitBytes, $AbsoluteLimitBytes)

    $violations = New-Object 'System.Collections.Generic.List[string]'
    foreach ($measurement in @($maximumByFunction.Values |
            Sort-Object -Property @{ Expression = 'Bytes'; Descending = $true }, Function)) {
        $limit = Get-FunctionLimit -Kind $measurement.Kind
        Write-Host ("  {0}: {1} bytes (function limit {2}, absolute limit {3})" -f `
            $measurement.Function, $measurement.Bytes, $limit, $AbsoluteLimitBytes)

        $reasons = New-Object 'System.Collections.Generic.List[string]'
        if ($measurement.Bytes -gt $limit) {
            $reasons.Add("function limit $limit")
        }
        if ($measurement.Bytes -gt $AbsoluteLimitBytes) {
            $reasons.Add("absolute limit $AbsoluteLimitBytes")
        }
        if ($reasons.Count -gt 0) {
            $violations.Add(("{0} uses {1} bytes, exceeding {2}" -f `
                $measurement.Function, $measurement.Bytes,
                ($reasons -join ' and ')))
        }
    }

    if ($violations.Count -gt 0) {
        Throw-GateFailure ($violations -join '; ')
    }
    Write-Host ("FX archive stack gate passed for {0} measured function(s)." -f `
        $maximumByFunction.Count)
}

try {
    Invoke-FxArchiveStackGate
}
catch {
    Write-Error $_.Exception.Message
    exit 1
}
