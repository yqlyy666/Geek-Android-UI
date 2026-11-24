param(
    [Parameter(Mandatory=$true)][string]$Source,
    [Parameter(Mandatory=$true)][string]$Output,
    [string]$Entry = 'native_probe',
    [string]$ImageBase = '0x140400000',
    [string]$ZigPath
)
$ErrorActionPreference = 'Stop'
if ($ZigPath) {
    $zigExecutable = (Resolve-Path -LiteralPath $ZigPath).Path
} else {
    $zigCommand = Get-Command zig -CommandType Application -ErrorAction SilentlyContinue
    if (-not $zigCommand) {
        throw 'Zig was not found on PATH. Install Zig 0.13.0 or pass -ZigPath with the path to zig.exe.'
    }
    $zigExecutable = $zigCommand.Source
}
$sourcePath = (Resolve-Path -LiteralPath $Source).Path
$outputPath = [System.IO.Path]::GetFullPath($Output)
$objectPath = [System.IO.Path]::ChangeExtension($outputPath, '.obj')
$compileArgs = @('cc', '-target', 'x86_64-windows-gnu', '-O2', '-g0', '-ffreestanding', '-fno-stack-protector', '-fno-builtin', '-funwind-tables', '-c', $sourcePath, '-o', $objectPath)
& $zigExecutable @compileArgs
if ($LASTEXITCODE -ne 0) { throw "C compilation failed ($LASTEXITCODE)" }
$linkArgs = @('build-exe', $objectPath, '-target', 'x86_64-windows-msvc', "-fentry=$Entry", '--subsystem', 'windows', '--image-base', $ImageBase, '-fno-compiler-rt', '-fno-PIE', '-fstrip', "-femit-bin=$outputPath")
& $zigExecutable @linkArgs
if ($LASTEXITCODE -ne 0) { throw "Native link failed ($LASTEXITCODE)" }
Get-Item -LiteralPath $outputPath | Select-Object FullName, Length
