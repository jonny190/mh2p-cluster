# Creates only a new, separately named VM. Run in Administrator PowerShell.
# The ISO, virtual disk, configuration and log stay under the Audi workspace.
[CmdletBinding()]
param([switch]$Start)
$ErrorActionPreference = 'Stop'
$vmName = 'Audi-QNX66-Ubuntu1204'
$repoRoot = Split-Path -Parent $PSScriptRoot
$workspaceRoot = Split-Path -Parent (Split-Path -Parent $repoRoot)
$vmRoot = [IO.Path]::GetFullPath((Join-Path $workspaceRoot 'tools/qnx66-vm'))
$isoPath = Join-Path $vmRoot 'downloads/ubuntu-12.04.5-desktop-amd64.iso'
$machineRoot = Join-Path $vmRoot $vmName
$diskPath = Join-Path $machineRoot 'ubuntu1204.vhdx'
$logPath = Join-Path $vmRoot 'create-vm.log'
$resultPath = Join-Path $vmRoot 'create-vm-result.json'
$workspacePrefix = [IO.Path]::GetFullPath($workspaceRoot).TrimEnd([char]92) + [IO.Path]::DirectorySeparatorChar
foreach ($targetPath in @($vmRoot,$isoPath,$machineRoot,$diskPath,$logPath,$resultPath)) {
    if (-not $targetPath.StartsWith($workspacePrefix,[StringComparison]::OrdinalIgnoreCase)) { throw 'VM path escaped workspace' }
}
New-Item -ItemType Directory -Path $vmRoot -Force | Out-Null
Start-Transcript -LiteralPath $logPath -Append
try {
    $isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
    if (-not $isAdmin) { throw 'Administrator PowerShell is required by Hyper-V.' }
    Import-Module Hyper-V
    if ((Get-Service vmms).Status -ne 'Running') { throw 'Hyper-V management service is not running' }
    $isoHash = (Get-FileHash -LiteralPath $isoPath -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($isoHash -ne 'd1f10ea7ca59266567fa8d2522ad800e1aa063f139630f925d2484d8e169b4c2') { throw 'Ubuntu ISO checksum mismatch' }
    if (Get-VM -Name $vmName -ErrorAction SilentlyContinue) { throw "VM already exists: $vmName. It has not been changed." }
    if (Test-Path -LiteralPath $machineRoot) { throw 'VM directory already exists; retaining it unchanged.' }
    $vmArguments = @{Name=$vmName;Generation=1;MemoryStartupBytes=4GB;Path=$machineRoot;NewVHDPath=$diskPath;NewVHDSizeBytes=32GB}
    $switch = Get-VMSwitch -Name 'Default Switch' -ErrorAction SilentlyContinue
    if ($switch) { $vmArguments.SwitchName = $switch.Name }
    $machine = New-VM @vmArguments
    Set-VMProcessor -VM $machine -Count 2
    Set-VMMemory -VM $machine -DynamicMemoryEnabled $false
    Set-VM -VM $machine -AutomaticStartAction Nothing -AutomaticStopAction ShutDown -CheckpointType Standard
    Set-VMDvdDrive -VMName $vmName -Path $isoPath
    Set-VMBios -VMName $vmName -StartupOrder @('CD','IDE','LegacyNetworkAdapter','Floppy')
    if ($Start) { Start-VM -VM $machine }
    [ordered]@{success=$true;name=$vmName;id=$machine.Id.ToString();generation=1;memoryGB=4;processors=2;diskGB=32;iso=$isoPath;isoSHA256=$isoHash;vmRoot=$machineRoot;networkSwitch=$(if($switch){$switch.Name}else{'disconnected'});started=[bool]$Start;ubuntuInstalled=$false;qnxInstalled=$false} | ConvertTo-Json | Set-Content -LiteralPath $resultPath -Encoding UTF8
    Write-Output "VM prepared: $vmName. Ubuntu installation is still required."
} catch {
    [ordered]@{success=$false;name=$vmName;error=$_.Exception.Message;log=$logPath} | ConvertTo-Json | Set-Content -LiteralPath $resultPath -Encoding UTF8
    Write-Error $_
    exit 1
} finally {
    Stop-Transcript
}
