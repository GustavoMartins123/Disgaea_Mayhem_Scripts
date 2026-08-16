$ErrorActionPreference = 'Stop'

$project = Split-Path -Parent $MyInvocation.MyCommand.Path
$gameRoot = Resolve-Path (Join-Path $project '..\..')
$outputDirectory = Join-Path $project 'build'
$objectDirectory = Join-Path $outputDirectory 'obj'
$output = Join-Path $outputDirectory 'DisgaeaMayhemModMenu.dll'
$gcc = 'C:\TDM-GCC-64\bin\gcc.exe'
$gxx = 'C:\TDM-GCC-64\bin\g++.exe'

# Prefer the project's TDM-GCC toolchain, but allow a MinGW toolchain from PATH
# so the same build is reproducible on CI and other Windows machines.
if (-not (Test-Path -LiteralPath $gcc -PathType Leaf)) {
    $gccCommand = Get-Command gcc.exe -ErrorAction SilentlyContinue
    if ($gccCommand) { $gcc = $gccCommand.Source }
}
if (-not (Test-Path -LiteralPath $gxx -PathType Leaf)) {
    $gxxCommand = Get-Command g++.exe -ErrorAction SilentlyContinue
    if ($gxxCommand) { $gxx = $gxxCommand.Source }
}

foreach ($compiler in @($gcc, $gxx)) {
    if (-not (Test-Path -LiteralPath $compiler -PathType Leaf)) {
        throw "Compilador MinGW obrigatorio ausente: $compiler"
    }
}

New-Item -ItemType Directory -Force $objectDirectory | Out-Null

$commonIncludes = @(
    "-I$project\vendor\minhook\include",
    "-I$project\vendor\minhook\src",
    "-I$project\vendor\imgui",
    "-I$project\vendor\imgui\backends"
)
$cFlags = @('-O2', '-DNDEBUG', '-D_WIN64', '-Wall', '-Wextra', '-Werror') + $commonIncludes
$cppFlags = @('-std=c++17', '-O2', '-DNDEBUG', '-D_WIN64', '-Wall', '-Wextra', '-Werror') + $commonIncludes

$cSources = @(
    'vendor\minhook\src\buffer.c',
    'vendor\minhook\src\hook.c',
    'vendor\minhook\src\trampoline.c',
    'vendor\minhook\src\hde\hde64.c'
)
$cppSources = @(
    'mod_menu_overlay.cpp',
    'vendor\imgui\imgui.cpp',
    'vendor\imgui\imgui_draw.cpp',
    'vendor\imgui\imgui_tables.cpp',
    'vendor\imgui\imgui_widgets.cpp',
    'vendor\imgui\backends\imgui_impl_dx12.cpp'
)
$objects = [System.Collections.Generic.List[string]]::new()

foreach ($source in $cSources) {
    $name = ($source -replace '[\\/:]', '_') + '.o'
    $object = Join-Path $objectDirectory $name
    & $gcc @cFlags -c (Join-Path $project $source) -o $object
    if ($LASTEXITCODE -ne 0) {
        throw "Falha ao compilar $source"
    }
    $objects.Add($object)
}

foreach ($source in $cppSources) {
    $name = ($source -replace '[\\/:]', '_') + '.o'
    $object = Join-Path $objectDirectory $name
    $sourceFlags = $cppFlags
    if ($source.StartsWith('vendor\imgui\')) {
        $sourceFlags = $sourceFlags + @(
            '-fpermissive',
            '-Wno-error'
        )
    }
    & $gxx @sourceFlags -c (Join-Path $project $source) -o $object
    if ($LASTEXITCODE -ne 0) {
        throw "Falha ao compilar $source"
    }
    $objects.Add($object)
}

& $gxx -shared -static-libgcc -static-libstdc++ -o $output @objects `
    -ld3d12 -ldxgi -ld3dcompiler -ldxguid -lxinput9_1_0 -luser32 -lkernel32
if ($LASTEXITCODE -ne 0) {
    throw 'Falha ao vincular DisgaeaMayhemModMenu.dll'
}

# Deploy to game directories
$targets = [System.Collections.Generic.List[string]]::new()
$targets.Add($gameRoot.ToString())

# Auto-discover from running process
$proc = Get-Process -Name "Disgaea_Mayhem" -ErrorAction SilentlyContinue | Select-Object -First 1
if ($proc) {
    try {
        $procDir = Split-Path -Parent $proc.Path
        if ((Test-Path (Join-Path $procDir 'Disgaea_Mayhem.exe')) -and ($targets -notcontains $procDir)) {
            $targets.Add($procDir)
        }
    } catch {}
}

# Auto-discover from all mounted drives (C:, D:, E:, F:, G:, etc.)
$drives = Get-PSDrive -PSProvider FileSystem | Select-Object -ExpandProperty Root
foreach ($drive in $drives) {
    $candidates = @(
        (Join-Path $drive 'Steam\steamapps\common\Disgaea Mayhem'),
        (Join-Path $drive 'SteamLibrary\steamapps\common\Disgaea Mayhem'),
        (Join-Path $drive 'Program Files (x86)\Steam\steamapps\common\Disgaea Mayhem'),
        (Join-Path $drive 'Games\Disgaea Mayhem')
    )
    foreach ($c in $candidates) {
        if ((Test-Path (Join-Path $c 'Disgaea_Mayhem.exe')) -and ($targets -notcontains $c)) {
            $targets.Add($c)
        }
    }
}

foreach ($target in $targets) {
    $targetNativeDll = Join-Path $target 'mods\native\DisgaeaMayhemModMenu.dll'
    $targetDxgiDll = Join-Path $target 'dxgi.dll'
    
    try {
        New-Item -ItemType Directory -Force (Join-Path $target 'mods\native') | Out-Null
        Copy-Item $output -Destination $targetNativeDll -Force
    } catch {
        Write-Warning "mods\native\DisgaeaMayhemModMenu.dll em uso em $target."
    }

    try {
        Copy-Item $output -Destination $targetDxgiDll -Force
        Write-Host "[OK] Proxy de auto-inicializacao atualizado em: $targetDxgiDll"
    } catch {
        Write-Warning "dxgi.dll em uso pelo jogo em $target (feche o jogo para atualizar o arquivo)."
    }
}

Write-Host "[OK] DLL nativo compilado com sucesso: $output"
