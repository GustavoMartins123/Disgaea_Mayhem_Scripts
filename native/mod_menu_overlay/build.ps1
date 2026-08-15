$ErrorActionPreference = 'Stop'

$project = Split-Path -Parent $MyInvocation.MyCommand.Path
$gameRoot = Resolve-Path (Join-Path $project '..\..')
$outputDirectory = Join-Path $project 'build'
$objectDirectory = Join-Path $outputDirectory 'obj'
$output = Join-Path $outputDirectory 'DisgaeaMayhemModMenu.dll'
$gcc = 'C:\TDM-GCC-64\bin\gcc.exe'
$gxx = 'C:\TDM-GCC-64\bin\g++.exe'

foreach ($compiler in @($gcc, $gxx)) {
    if (-not (Test-Path -LiteralPath $compiler -PathType Leaf)) {
        throw "Compilador obrigatorio ausente: $compiler"
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
$gameNativeDll = Join-Path $gameRoot 'mods\native\DisgaeaMayhemModMenu.dll'
$gameDxgiDll = Join-Path $gameRoot 'dxgi.dll'

try {
    Copy-Item $output -Destination $gameNativeDll -Force
} catch {
    Write-Warning "mods\native\DisgaeaMayhemModMenu.dll em uso pelo jogo."
}

try {
    Copy-Item $output -Destination $gameDxgiDll -Force
    Write-Host "[OK] Proxy de auto-inicializacao atualizado: $gameDxgiDll"
} catch {
    Write-Warning "dxgi.dll em uso pelo jogo (feche o jogo para atualizar o arquivo)."
}

Write-Host "[OK] DLL nativo compilado com sucesso: $output"
