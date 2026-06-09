$ErrorActionPreference = 'Stop'

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectRoot = Resolve-Path (Join-Path $ScriptDir '..\..')

$Platform = 'mingw'
$Arch = 'x86'
$CompilerKind = ''
$BuildDir = ''
$InstallPrefix = ''
$FallbackDir = ''
$ProductName = ''
$OsSdkArchive = ''
$PromptedProduct = $false
$NoRun = $false
$CheckPlatformDeps = $false
$PauseOnExit = $true
if ($env:FLOATAIR_NO_PAUSE) { $PauseOnExit = $false }

$ColorGreen = ''
$ColorRed = ''
$ColorReset = ''
if (-not $env:NO_COLOR) {
    $esc = [char]27
    $ColorGreen = "$esc[32m"
    $ColorRed = "$esc[31m"
    $ColorReset = "$esc[0m"
}

function Write-Ok([string]$Message) {
    Write-Host "[SUCCESS] $Message" -ForegroundColor Green
}

function Write-Err([string]$Message) {
    Write-Host "[ERROR] $Message" -ForegroundColor Red
}

function Status-Text([bool]$Ok) {
    if ($Ok) { return "${ColorGreen}yes${ColorReset}" }
    return "${ColorRed}no${ColorReset}"
}

function Exit-Script([int]$Code) {
    if ($PauseOnExit) {
        Read-Host 'Press Enter to continue...'
    }
    exit $Code
}

function Die([string]$Message) {
    Write-Err $Message
    Exit-Script 1
}

trap {
    Write-Err $_.Exception.Message
    Exit-Script 1
}

function Usage {
    Write-Host 'Usage: develop-simulator-mingw.bat [OS_SDK_ARCHIVE] [--arch x86|x64] [--compiler gcc|llvm] [--build-dir DIR] [--prefix DIR] [--product PRODUCT] [--no_run] [--check-platform-deps] [--no-pause]'
    Write-Host '       develop-simulator-msvc.bat [OS_SDK_ARCHIVE] [--arch x86|x64] [--compiler cl|clang|clang-cl] [--build-dir DIR] [--prefix DIR] [--product PRODUCT] [--no_run] [--check-platform-deps] [--no-pause]'
    Write-Host '       develop-simulator.ps1 [OS_SDK_ARCHIVE] [--platform mingw|msvc] [--arch x86|x64] [--compiler COMPILER] [--fallback-dir DIR] [--build-dir DIR] [--prefix DIR] [--product PRODUCT] [--no_run] [--check-platform-deps] [--no-pause]'
    Write-Host ''
    Write-Host 'Defaults: --platform mingw --arch x86 --compiler gcc.'
    Write-Host 'MinGW compilers: gcc, llvm.'
    Write-Host 'MSVC compilers: cl, clang, clang-cl.'
}

function Need-Value([string]$Name, [object[]]$Values, [int]$Index) {
    if ($Index + 1 -ge $Values.Count) {
        Die "$Name requires a value."
    }
}

function Find-Program([string]$Name) {
    $cmd = Get-Command $Name -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    return $null
}

function Select-Product {
    $productsDir = Join-Path $ProjectRoot 'products'
    $products = @(Get-ChildItem -Path $productsDir -Directory -ErrorAction SilentlyContinue | Sort-Object Name)
    if ($products.Count -eq 0) {
        Die "No products found in `"$productsDir`"."
    }

    Write-Host ''
    Write-Host 'Select product:'
    for ($i = 0; $i -lt $products.Count; $i++) {
        Write-Host ("  {0}. {1}" -f ($i + 1), $products[$i].Name)
    }

    while ($true) {
        $choice = Read-Host 'Enter product number'
        $index = 0
        if ([int]::TryParse($choice, [ref]$index) -and $index -ge 1 -and $index -le $products.Count) {
            return $products[$index - 1].Name
        }
        Write-Err 'Invalid product selection.'
    }
}

function Select-OsSdkArchive {
    Write-Host ''
    $path = Read-Host 'Enter OS SDK archive path (empty to use newest cache)'
    if ($path) { return $path.Trim('"') }
    return ''
}

function Read-FirstLine([string]$Exe, [string[]]$ArgumentList) {
    if (-not (Test-Path $Exe) -and -not (Get-Command $Exe -ErrorAction SilentlyContinue)) {
        return $null
    }
    try {
        $out = & $Exe @ArgumentList 2>$null
        if ($out) { return [string]($out | Select-Object -First 1) }
    } catch {
        return $null
    }
    return $null
}

for ($i = 0; $i -lt $args.Count; $i++) {
    switch -Regex ($args[$i]) {
        '^--platform$' {
            Need-Value $args[$i] $args $i
            $Platform = $args[++$i]
            continue
        }
        '^--arch$' {
            Need-Value $args[$i] $args $i
            $Arch = $args[++$i]
            continue
        }
        '^--compiler$' {
            Need-Value $args[$i] $args $i
            $CompilerKind = $args[++$i]
            continue
        }
        '^--build-dir$' {
            Need-Value $args[$i] $args $i
            $BuildDir = $args[++$i]
            continue
        }
        '^--prefix$' {
            Need-Value $args[$i] $args $i
            $InstallPrefix = $args[++$i]
            continue
        }
        '^--fallback-dir$' {
            Need-Value $args[$i] $args $i
            $FallbackDir = $args[++$i]
            continue
        }
        '^--product$' {
            Need-Value $args[$i] $args $i
            $ProductName = $args[++$i]
            continue
        }
        '^--no_run$|^--no-run$' {
            $NoRun = $true
            continue
        }
        '^--check-platform-deps$' {
            $CheckPlatformDeps = $true
            continue
        }
        '^--no-pause$' {
            $PauseOnExit = $false
            continue
        }
        '^-h$|^--help$' {
            Usage
            Exit-Script 0
        }
        default {
            if ((-not $OsSdkArchive) -and ($args[$i] -notmatch '^-')) {
                $OsSdkArchive = $args[$i]
                continue
            }
            Usage
            Die "Unknown argument: $($args[$i])"
        }
    }
}

switch -Regex ($Platform) {
    '^(mingw)$' { $Platform = 'mingw' }
    '^(msvc)$' { $Platform = 'msvc' }
    default { Die "Unsupported platform: $Platform. Use mingw or msvc." }
}

switch -Regex ($Arch) {
    '^(x86|i386|i686|x86_32|32)$' {
        $Arch = 'x86'
        $MingwTriple = 'i686-w64-mingw32'
        $VsArch = 'x86'
        $MsvcClangTarget = 'i686-pc-windows-msvc'
    }
    '^(x64|amd64|x86_64|64)$' {
        $Arch = 'x64'
        $MingwTriple = 'x86_64-w64-mingw32'
        $VsArch = 'x64'
        $MsvcClangTarget = 'x86_64-pc-windows-msvc'
    }
    default { Die "Unsupported arch: $Arch. Use x86 or x64." }
}

if (-not $CompilerKind) {
    $CompilerKind = if ($Platform -eq 'mingw') { 'gcc' } else { 'cl' }
}

if ($Platform -eq 'mingw') {
    switch -Regex ($CompilerKind) {
        '^gcc$' { $CompilerKind = 'gcc' }
        '^(llvm|clang)$' { $CompilerKind = 'llvm' }
        default { Die "Unsupported MinGW compiler: $CompilerKind. Use gcc or llvm." }
    }
} else {
    switch -Regex ($CompilerKind) {
        '^cl$' { $CompilerKind = 'cl' }
        '^(llvm|clang)$' { $CompilerKind = 'clang' }
        '^clang-cl$' { $CompilerKind = 'clang-cl' }
        default { Die "Unsupported MSVC compiler: $CompilerKind. Use cl, clang, or clang-cl." }
    }
}

$PlatformTitle = if ($Platform -eq 'mingw') { 'Windows MinGW' } else { 'Windows MSVC' }
$BuildTitle = if ($Platform -eq 'mingw') { 'Windows MinGW' } else { 'MSVC' }
$BuildStem = $Platform
$SdlLibDir = $Platform
$SdlImportLib = if ($Platform -eq 'mingw') { 'libSDL2.dll.a' } else { 'SDL2.lib' }

function Test-CompilerArch([string]$Compiler) {
    $dump = Read-FirstLine $Compiler @('-dumpmachine')
    if (-not $dump) { return $true }
    if ($Arch -eq 'x86') { return [bool]($dump -match 'i686') }
    return [bool]($dump -match 'x86_64')
}

function Test-CompilerKind([string]$Compiler, [string]$Kind) {
    $version = Read-FirstLine $Compiler @('--version')
    if (-not $version) { return $false }
    if ($Kind -eq 'gcc') { return [bool]($version -notmatch 'clang') }
    return [bool]($version -match 'clang')
}

function Find-CompilerInList([string]$EnvName, [string]$Exe, [string]$Kind) {
    $value = [Environment]::GetEnvironmentVariable($EnvName)
    if (-not $value) { return $null }
    foreach ($dir in $value -split ';') {
        if (-not $dir) { continue }
        $candidate = Join-Path $dir $Exe
        if ((Test-Path $candidate) -and ((Test-CompilerArch $candidate) -eq $true) -and ((Test-CompilerKind $candidate $Kind) -eq $true)) {
            return $candidate
        }
    }
    return $null
}

function Find-MingwCompiler([string]$Exe, [string]$Kind) {
    $envName = if ($Kind -eq 'gcc') { 'MINGW' } else { 'LLVM' }
    $found = Find-CompilerInList $envName $Exe $Kind
    if ($found) { return $found }

    $cmd = Find-Program $Exe
    if ($cmd -and ((Test-CompilerArch $cmd) -eq $true) -and ((Test-CompilerKind $cmd $Kind) -eq $true)) {
        return $cmd
    }

    if ($FallbackDir) {
        $candidate = Join-Path $FallbackDir $Exe
        if ((Test-Path $candidate) -and ((Test-CompilerArch $candidate) -eq $true) -and ((Test-CompilerKind $candidate $Kind) -eq $true)) {
            return $candidate
        }
    }
    return $null
}

function Test-MingwCanLink([string]$Compiler) {
    $probe = Join-Path ([IO.Path]::GetTempPath()) "floatair-mingw-probe-$PID-$([Guid]::NewGuid().ToString('N'))"
    New-Item -ItemType Directory -Path $probe | Out-Null
    try {
        $source = Join-Path $probe 'probe.c'
        $exe = Join-Path $probe 'probe.exe'
        'int main(void) { return 0; }' | Set-Content -Encoding ASCII $source
        & $Compiler $source -o $exe > $null 2>&1
        return $LASTEXITCODE -eq 0
    } finally {
        Remove-Item -Recurse -Force $probe -ErrorAction SilentlyContinue
    }
}

function Find-VsDevCmd {
    if ($env:VSDEVCMD -and (Test-Path $env:VSDEVCMD)) { return $env:VSDEVCMD }

    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path $vswhere)) {
        $vswhere = Join-Path $env:ProgramFiles 'Microsoft Visual Studio\Installer\vswhere.exe'
    }
    if (Test-Path $vswhere) {
        $root = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        if ($root) {
            $candidate = Join-Path $root 'Common7\Tools\VsDevCmd.bat'
            if (Test-Path $candidate) { return $candidate }
        }
    }

    foreach ($base in @(
        (Join-Path $env:ProgramFiles 'Microsoft Visual Studio\18'),
        (Join-Path $env:ProgramFiles 'Microsoft Visual Studio\2022'),
        (Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\2019'),
        $FallbackDir
    )) {
        if (-not $base) { continue }
        foreach ($edition in @('', 'Community', 'Professional', 'Enterprise', 'BuildTools')) {
            $root = if ($edition) { Join-Path $base $edition } else { $base }
            $candidate = Join-Path $root 'Common7\Tools\VsDevCmd.bat'
            if (Test-Path $candidate) { return $candidate }
        }
    }
    return $null
}

function Get-VsRoot([string]$VsDevCmd) {
    if (-not $VsDevCmd) { return $null }
    return (Resolve-Path (Join-Path (Split-Path -Parent $VsDevCmd) '..\..')).Path
}

function Current-VsArch {
    if ($env:VSCMD_ARG_TGT_ARCH -match '^(x64|amd64)$') { return 'x64' }
    if ($env:VSCMD_ARG_TGT_ARCH -eq 'x86') { return 'x86' }
    return ''
}

function Test-ClArch([string]$Compiler) {
    if ($Arch -eq 'x86') { return $Compiler -match '\\Hostx(64|86)\\x86\\cl.exe$' }
    return $Compiler -match '\\Hostx(64|86)\\x64\\cl.exe$'
}

function Test-VsLlvm([string]$Compiler) {
    $dir = Split-Path -Parent $Compiler
    return (Test-Path (Join-Path $dir 'lld-link.exe')) -or ($dir -match '\\VC\\Tools\\Llvm\\(bin|x64\\bin|x86\\bin)$')
}

function Find-VsLlvmCompiler([string]$Exe, [string]$VsRoot) {
    if (-not $VsRoot) { return $null }
    foreach ($dir in @('VC\Tools\Llvm\bin', 'VC\Tools\Llvm\x64\bin', 'VC\Tools\Llvm\x86\bin')) {
        $candidate = Join-Path (Join-Path $VsRoot $dir) $Exe
        if (Test-Path $candidate) { return $candidate }
    }
    return $null
}

function Import-VsEnv([string]$VsDevCmd) {
    $before = Get-ChildItem Env: | ForEach-Object { @{ Name = $_.Name; Value = $_.Value } }
    cmd /c "`"$VsDevCmd`" -arch=$VsArch -host_arch=x64 >nul && set" | ForEach-Object {
        if ($_ -match '^(.*?)=(.*)$') {
            Set-Item -Path "Env:$($matches[1])" -Value $matches[2]
        }
    }
}

function Ensure-MsvcEnv {
    if ((Current-VsArch) -eq $Arch) { return $true }
    $script:VsDevCmd = Find-VsDevCmd
    if (-not $script:VsDevCmd) { return $false }
    Write-Host "[INFO] Loading Visual Studio environment: `"$script:VsDevCmd`" -arch=$VsArch -host_arch=x64"
    Import-VsEnv $script:VsDevCmd
    return $true
}

function Find-MsvcCompiler([bool]$ForBuild) {
    if ($CompilerKind -eq 'cl') {
        $cl = Find-Program 'cl.exe'
        if ($cl -and (Test-ClArch $cl)) { return $cl }
        if (-not $ForBuild -and $script:VsDevCmd) { return "cl.exe via $script:VsDevCmd" }
        return $null
    }

    $exe = "$CompilerKind.exe"
    $cmd = Find-Program $exe
    if ($cmd -and (Test-VsLlvm $cmd)) { return $cmd }
    return Find-VsLlvmCompiler $exe (Get-VsRoot $script:VsDevCmd)
}

function Check-Program([string]$Name) {
    $tool = Find-Program $Name
    if ($tool) {
        Write-Host "[CHECK] ${Name}: $(Status-Text $true) ($tool)"
        return $true
    }
    Write-Host "[CHECK] ${Name}: $(Status-Text $false)"
    return $false
}

function Check-File([string]$Label, [string]$Path) {
    $ok = Test-Path $Path
    Write-Host "[CHECK] ${Label}: $(Status-Text $ok)"
    return $ok
}

function Check-PlatformDeps {
    $failed = $false
    Write-Host "[INFO] Checking $PlatformTitle simulator deps: arch=$Arch compiler=$CompilerKind"
    if (-not (Check-Program cmake)) { $failed = $true }
    if (-not (Check-Program ninja)) { $failed = $true }

    if ($Platform -eq 'mingw') {
        $exe = if ($CompilerKind -eq 'gcc') { "$MingwTriple-gcc.exe" } else { "$MingwTriple-clang.exe" }
        $kind = if ($CompilerKind -eq 'gcc') { 'gcc' } else { 'llvm' }
        $compiler = Find-MingwCompiler $exe $kind
        if ($compiler) {
            Write-Host "[CHECK] compiler: $(Status-Text $true) (`"$compiler`")"
            $canLink = Test-MingwCanLink $compiler
            Write-Host "[CHECK] compiler link Windows ${Arch}: $(Status-Text $canLink)"
            if (-not $canLink) { $failed = $true }
        } else {
            Write-Host "[CHECK] compiler: $(Status-Text $false) ($exe)"
            $failed = $true
        }
    } else {
        $script:VsDevCmd = Find-VsDevCmd
        $vsOk = ((Current-VsArch) -eq $Arch) -or [bool]$script:VsDevCmd
        $vsInfo = if ((Current-VsArch) -eq $Arch) { " (current $Arch)" } elseif ($script:VsDevCmd) { " (`"$script:VsDevCmd`")" } else { '' }
        Write-Host "[CHECK] Visual Studio environment: $(Status-Text $vsOk)$vsInfo"
        if (-not $vsOk -and $CompilerKind -ne 'cl') { $failed = $true }
        $compiler = Find-MsvcCompiler $false
        if ($compiler) { Write-Host "[CHECK] compiler: $(Status-Text $true) (`"$compiler`")" }
        else {
            Write-Host "[CHECK] compiler: $(Status-Text $false) ($CompilerKind.exe)"
            $failed = $true
        }
    }

    $sdlBase = Join-Path $ScriptDir "windows\SDL2\lib\$SdlLibDir\$Arch"
    if (-not (Check-File 'bundled SDL2 CMake config' (Join-Path $ScriptDir 'windows\SDL2\cmake\sdl2-config.cmake'))) { $failed = $true }
    if (-not (Check-File "bundled SDL2.dll $Arch" (Join-Path $sdlBase 'SDL2.dll'))) { $failed = $true }
    if (-not (Check-File "bundled SDL2 import lib $Arch" (Join-Path $sdlBase $SdlImportLib))) { $failed = $true }

    if ($failed) {
        Write-Err "$PlatformTitle simulator deps are incomplete."
        Exit-Script 1
    }
    Write-Ok "$PlatformTitle simulator deps are available."
}

function Prepare-Compiler {
    if ($Platform -eq 'mingw') {
        $exe = if ($CompilerKind -eq 'gcc') { "$MingwTriple-gcc.exe" } else { "$MingwTriple-clang.exe" }
        $kind = if ($CompilerKind -eq 'gcc') { 'gcc' } else { 'llvm' }
        $compiler = Find-MingwCompiler $exe $kind
        if (-not $compiler) { Die "$PlatformTitle compiler was not found for $Arch`: $exe" }
        $env:Path = "$(Split-Path -Parent $compiler);$env:Path"
        Write-Host "[INFO] Added compiler directory to PATH: `"$(Split-Path -Parent $compiler)`""
        Write-Host "[INFO] Using MinGW $CompilerKind`: `"$compiler`""
        return @{ Compiler = $compiler; TargetArg = $null }
    }

    if (-not (Ensure-MsvcEnv)) { Die "Visual Studio developer environment for $Arch was not found." }
    $compiler = Find-MsvcCompiler $true
    if (-not $compiler) { Die "MSVC compiler was not found: $CompilerKind.exe" }
    if ($CompilerKind -eq 'cl') {
        Write-Host "[INFO] Using MSVC cl: `"$compiler`""
        return @{ Compiler = $compiler; TargetArg = $null }
    }
    Write-Host "[INFO] Using Visual Studio LLVM: `"$compiler`""
    Write-Host "[INFO] Using MSVC LLVM target: $MsvcClangTarget"
    return @{ Compiler = $compiler; TargetArg = "-DCMAKE_C_COMPILER_TARGET=$MsvcClangTarget" }
}

Set-Location $ProjectRoot

if ($CheckPlatformDeps) {
    Check-PlatformDeps
    Exit-Script 0
}

if (-not $ProductName) {
    $ProductName = Select-Product
    $PromptedProduct = $true
}
Write-Host "[INFO] Selected product: $ProductName"

if ((-not $OsSdkArchive) -and $PromptedProduct) {
    $OsSdkArchive = Select-OsSdkArchive
}

if ($OsSdkArchive) {
    Write-Host "[INFO] Using OS SDK archive: `"$OsSdkArchive`""
} else {
    Write-Host '[INFO] Using newest OS SDK cache.'
}

$compilerInfo = Prepare-Compiler
if (-not $BuildDir) { $BuildDir = "simulator\FloatairSimulator\build-$BuildStem-$Arch-$CompilerKind" }
if (-not $InstallPrefix) { $InstallPrefix = "install\$BuildStem-$Arch-$CompilerKind" }
$BuildDirAbs = if ([IO.Path]::IsPathRooted($BuildDir)) { $BuildDir } else { Join-Path $ProjectRoot $BuildDir }

if (-not (Find-Program cmake)) { Die 'cmake was not found in PATH.' }
if (-not (Find-Program ninja)) { Die 'ninja was not found in PATH.' }
if (Test-Path $BuildDir) {
    Write-Host "[INFO] Removing build directory `"$BuildDir`"..."
    Remove-Item -Recurse -Force $BuildDir
}

$cmakeArgs = @(
    '-S', '.', '-B', $BuildDir, '-G', 'Ninja',
    '-DCMAKE_BUILD_TYPE=Debug',
    "-DCMAKE_INSTALL_PREFIX=$InstallPrefix",
    "-DCMAKE_C_COMPILER=$($compilerInfo.Compiler)",
    "-DJY_APP_PRODUCT=$ProductName"
)
if ($compilerInfo.TargetArg) { $cmakeArgs += $compilerInfo.TargetArg }
if ($OsSdkArchive) { $cmakeArgs += "-DJY_APP_OS_SDK_ARCHIVE=$OsSdkArchive" }

Write-Host "[INFO] Configuring $BuildTitle $Arch simulator build with $CompilerKind in `"$BuildDir`"..."
& cmake @cmakeArgs
if ($LASTEXITCODE -ne 0) { Exit-Script $LASTEXITCODE }

Write-Host "[INFO] Building $BuildTitle simulator..."
& ninja -C $BuildDir
if ($LASTEXITCODE -ne 0) { Exit-Script $LASTEXITCODE }

$simulatorExe = Join-Path $BuildDirAbs 'floatair_simulator.exe'
if (-not (Test-Path $simulatorExe)) { Die "Simulator executable not found: `"$simulatorExe`"" }

Write-Ok "$BuildTitle simulator build finished."
Write-Host "[INFO] Installing $BuildTitle simulator to `"$InstallPrefix`"..."
& ninja -C $BuildDir install
if ($LASTEXITCODE -ne 0) { Exit-Script $LASTEXITCODE }

if ($NoRun) {
    Write-Host '[INFO] --no_run specified; skipping simulator launch.'
    Write-Host "[INFO] Output: `"$simulatorExe`""
    Exit-Script 0
}

Write-Host "[INFO] Starting: `"$simulatorExe`""
Start-Process -FilePath $simulatorExe -WorkingDirectory $BuildDirAbs
Exit-Script 0
