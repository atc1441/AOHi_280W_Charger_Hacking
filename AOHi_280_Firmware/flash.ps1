<#
  flash.ps1 - flash the AOHi 280W charger's display MCU (HC32F460xE) over J-Link.

  The HC32F460xE is natively supported by J-Link (built-in flash algorithm), so this
  is just a thin wrapper around JLink.exe's loadbin + reset+run. No RAM loader, no
  clock dance, no unlock - J-Link handles it all.

  Usage (run from this folder):
    .\flash.ps1                          # build\firmware.bin -> app slot @ 0x8000
    .\flash.ps1 -File fw.bin -Addr 0x0   # flash a specific image at an address
    .\flash.ps1 -Action erase            # full chip erase
    .\flash.ps1 -Action reset            # just reset+run the target
#>
[CmdletBinding()]
param(
    [ValidateSet('flash','erase','reset','read')]
    [string]$Action = 'flash',
    [string]$File = 'build\firmware.bin',
    [string]$Addr = '0x8000',
    [string]$Out  = 'read.bin',
    [string]$Size = '0x1000',
    [int]$Speed = 4000
)
$ErrorActionPreference = 'Stop'

$DEVICE = 'HC32F460xE'
$jl = @('C:\Program Files\SEGGER\JLink\JLink.exe','C:\Program Files (x86)\SEGGER\JLink\JLink.exe') |
      Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $jl) { throw 'JLink.exe not found (install SEGGER J-Link software)' }

$cmds = @('power on', 'si SWD', "speed $Speed", "device $DEVICE", 'connect')
switch ($Action) {
    'flash' {
        $img = (Resolve-Path $File).Path
        Write-Host "Flashing $img @ $Addr on $DEVICE"
        $cmds += @("loadbin `"$img`",$Addr", 'r', 'g')
    }
    'erase' { Write-Host "Erasing $DEVICE"; $cmds += 'erase' }
    'reset' { $cmds += @('r', 'g') }
    'read'  { $cmds += @("savebin `"$Out`",$Addr,$Size") }
}
$cmds += 'exit'

$tmp = Join-Path $env:TEMP ("hc32_{0}.jlink" -f $Action)
$cmds -join "`n" | Set-Content -Path $tmp -Encoding ascii
$o = & $jl -CommandFile $tmp 2>&1 | ForEach-Object { "$_" }
$o | Where-Object { $_ -match 'O\.K\.|fail|error|Verify|Programming|Erasing|Could not|RAMCode|Script' }
if ($o -match 'Could not|fail|error|FAILED') { throw 'J-Link reported a problem (see output above)' }
Write-Host 'Done.'
