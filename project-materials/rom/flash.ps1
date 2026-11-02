param([switch]$Check)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
Set-Location $PSScriptRoot
$script:Fastboot = Join-Path $PSScriptRoot 'bin/windows/fastboot.exe'
$script:Adb = Join-Path $PSScriptRoot 'bin/windows/adb.exe'
$script:Serial = ''
function Invoke-Tool([string]$Exe,[string[]]$Arguments) {
    $oldPreference = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try { $result = & $Exe @Arguments 2>&1 | Out-String; $code = $LASTEXITCODE }
    finally { $ErrorActionPreference = $oldPreference }
    if ($code -ne 0) {
        $shown = @($Arguments)
        for ($i=0; $i -lt ($shown.Count-1); $i++) {
            if ($shown[$i] -in @('-s','--serial')) {
                if ($shown[$i+1]) { $result = $result.Replace($shown[$i+1],'<DEVICE>') }
                $shown[$i+1] = '<DEVICE>'
            }
        }
        $message = "Command failed: $(Split-Path $Exe -Leaf) $shown`n$result"
        throw $message.Replace($PSScriptRoot,'<ROM_DIR>')
    }
    return $result
}
function Invoke-Fb([string[]]$Arguments) { return Invoke-Tool -Exe $script:Fastboot -Arguments (@('-s',$script:Serial)+$Arguments) }
function Get-FbVar([string]$Name) {
    $s = Invoke-Fb -Arguments @('getvar',$Name)
    $m = [regex]::Match($s,([regex]::Escape($Name)+':\s*(\S+)'))
    if (-not $m.Success) { throw "Cannot read bootloader parameter: $Name" }
    return $m.Groups[1].Value
}
function One-Serial([string]$Text,[string]$State) {
    $m = @([regex]::Matches($Text,('(?m)^(\S+)\s+'+$State+'\s*$')))
    if ($m.Count -ne 1) { throw 'Connect exactly one authorized tablet.' }
    return $m[0].Groups[1].Value
}
function Hash-Matches([string]$Path,$Row) {
    return ((Test-Path -LiteralPath $Path) -and (Get-Item -LiteralPath $Path).Length -eq $Row.bytes -and (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLower() -eq $Row.sha256)
}
try {
    Write-Host 'TB371FC ONLY / Xiaoxin Pad Pro 12.7 2023 Snapdragon 870. Candidate ROM.'
    $manifest = Get-Content -LiteralPath 'flash-manifest.json' -Raw -Encoding UTF8 | ConvertFrom-Json
    foreach ($row in $manifest.files) { if (-not (Hash-Matches $row.path $row)) { throw "Checksum failed, nothing flashed: $($row.path)" } }
    if ($Check) { Write-Host 'PASS: local verification only; device untouched.'; exit 0 }
    if ((Read-Host 'Type TB371FC to confirm the physical model') -cne 'TB371FC') { throw 'Cancelled' }
    $mode = Read-Host '1 Keep data (same port base only); 2 ERASE ALL DATA (first install)'
    if ($mode -notin @('1','2')) { throw 'Cancelled' }
    if ($mode -eq '2' -and (Read-Host 'Type ERASE to confirm userdata and metadata deletion') -cne 'ERASE') { throw 'Cancelled' }
    if ($mode -eq '1') {
        $upgradeSerial = One-Serial (Invoke-Tool -Exe $script:Adb -Arguments @('devices')) 'device'
        $base = (Invoke-Tool -Exe $script:Adb -Arguments @('-s',$upgradeSerial,'shell','getprop','ro.build.version.incremental')).Trim()
        $root = Invoke-Tool -Exe $script:Adb -Arguments @('-s',$upgradeSerial,'shell','su','0','sha256sum','/data/adb/ksud')
        if ($base -ne $manifest.upgrade_base_incremental -or $root -notmatch ([regex]::Escape($manifest.root_ksud_sha256)+'\s+/data/adb/ksud\b')) { throw 'Keep-data upgrade requires the adapted port and matching KSU. Old Magisk/other bases require a separate migration. Nothing flashed.' }
    }
    $raw = Join-Path $PSScriptRoot 'images/super.img'
    if (Test-Path -LiteralPath $raw) {
        if (-not (Hash-Matches $raw $manifest.super_raw)) { throw 'Existing super.img mismatches. Move it away and retry.' }
    } else {
        $drive = Get-PSDrive -Name ((Get-Item -LiteralPath $PSScriptRoot).PSDrive.Name)
        if ($drive.Free -lt ($manifest.super_raw.bytes+536870912)) { throw 'At least 13GiB free space is required.' }
        $part = $raw+'.part'
        Invoke-Tool -Exe (Join-Path $PSScriptRoot 'bin/windows/zstd.exe') -Arguments @('-d','-f','images/super.img.zst','-o',$part) | Write-Host
        if (-not (Hash-Matches $part $manifest.super_raw)) { throw 'Decompressed super hash failed. Nothing flashed.' }
        Move-Item -LiteralPath $part -Destination $raw -Force
    }
    $devices = Invoke-Tool -Exe $script:Fastboot -Arguments @('devices')
    if ([string]::IsNullOrWhiteSpace($devices)) {
        $script:Serial = One-Serial (Invoke-Tool -Exe $script:Adb -Arguments @('devices')) 'device'
        Invoke-Tool -Exe $script:Adb -Arguments @('-s',$script:Serial,'reboot','bootloader') | Write-Host
        for ($i=0;$i -lt 30;$i++) { Start-Sleep 1; $devices = Invoke-Tool -Exe $script:Fastboot -Arguments @('devices'); if (-not [string]::IsNullOrWhiteSpace($devices)) { break } }
    }
    $script:Serial = One-Serial $devices 'fastboot'
    $product = (Get-FbVar 'product').Trim().ToLowerInvariant()
    Write-Host "Bootloader product: $product"
    if ($product -notin @('kona','tb371fc')) { throw "Wrong product '$product'. Expected kona or tb371fc. Nothing flashed. Run bin\windows\fastboot.exe getvar product and verify the physical model before retrying." }
    if ((Get-FbVar 'unlocked').ToLower() -notin @('yes','true')) { throw 'Bootloader unlock could not be confirmed.' }
    foreach ($p in @(@('super',12884901888L),@('boot_a',100663296L),@('dtbo_a',25165824L))) {
        $n = Get-FbVar ('partition-size:'+$p[0])
        if ([Convert]::ToInt64(($n -replace '^0x',''),16) -ne $p[1]) { throw "Wrong partition size: $($p[0])" }
    }
    Invoke-Fb -Arguments @('set_active','a') | Write-Host
    foreach ($name in 'abl aop bluetooth boot cmnlib64 cmnlib devcfg dsp dtbo hyp imagefv keymaster modem qupfw recovery tz xbl xbl_config'.Split(' ')) {
        foreach ($slot in @('a','b')) { Invoke-Fb -Arguments @('flash',($name+'_'+$slot),('images/'+$name+'.img')) | Write-Host }
    }
    foreach ($name in @('vbmeta','vbmeta_system')) {
        foreach ($slot in @('a','b')) { Invoke-Fb -Arguments @('--disable-verity','--disable-verification','flash',($name+'_'+$slot),('images/'+$name+'.img')) | Write-Host }
    }
    Invoke-Fb -Arguments @('reboot','bootloader') | Write-Host
    Start-Sleep 8
    if ($mode -eq '2') { Invoke-Fb -Arguments @('erase','userdata') | Write-Host; Invoke-Fb -Arguments @('erase','metadata') | Write-Host }
    Invoke-Fb -Arguments @('flash','super',$raw) | Write-Host
    Invoke-Fb -Arguments @('reboot') | Write-Host
    Write-Host 'Images flashed. Auto setup can reboot twice. Keep USB connected.'
    $deadline = (Get-Date).AddMinutes(20)
    while ((Get-Date) -lt $deadline) {
        Start-Sleep 5
        try { $state = (Invoke-Tool -Exe $script:Adb -Arguments @('-s',$script:Serial,'shell','getprop','sys.tb371fc.setup')).Trim() } catch { continue }
        if ($state -eq 'ready') { Write-Host 'Auto deployment and file checks passed. Unlock and test actual functions.'; exit 0 }
        if ($state -in @('failed','disabled','safe_mode')) { throw "Setup stopped: $state. Preserve logs and read recovery guide." }
    }
    throw 'Setup timed out. Do not report success; inspect the tablet and recovery guide.'
} catch { Write-Host "STOPPED: $_"; exit 1 }
