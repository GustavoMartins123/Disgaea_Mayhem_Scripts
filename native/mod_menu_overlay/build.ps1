$ErrorActionPreference = 'Stop'

$menuProject = Split-Path -Parent $MyInvocation.MyCommand.Path
$nativeRoot = Resolve-Path (Join-Path $menuProject '..')
$gameRoot = Resolve-Path (Join-Path $nativeRoot '..')
$loaderProject = Join-Path $nativeRoot 'mod_loader'
$buildRoot = Join-Path $menuProject 'build'
$menuObjectRoot = Join-Path $buildRoot 'obj-menu'
$loaderObjectRoot = Join-Path $buildRoot 'obj-loader'
$pluginObjectRoot = Join-Path $buildRoot 'obj-plugins'
$gcc = 'C:\TDM-GCC-64\bin\gcc.exe'
$gxx = 'C:\TDM-GCC-64\bin\g++.exe'

foreach ($compiler in @($gcc, $gxx)) {
    if (-not (Test-Path -LiteralPath $compiler -PathType Leaf)) {
        throw "Compilador MinGW obrigatorio ausente: $compiler"
    }
}

New-Item -ItemType Directory -Force $menuObjectRoot, $loaderObjectRoot, $pluginObjectRoot | Out-Null

$includes = @(
    "-I$menuProject\vendor\minhook\include",
    "-I$menuProject\vendor\minhook\src",
    "-I$menuProject\vendor\imgui",
    "-I$menuProject\vendor\imgui\backends",
    "-I$loaderProject"
)
$cFlags = @('-O2', '-DNDEBUG', '-D_WIN64', '-Wall', '-Wextra', '-Werror') + $includes
$cppFlags = @('-std=c++17', '-O2', '-DNDEBUG', '-D_WIN64', '-Wall', '-Wextra', '-Werror') + $includes

function Compile-CObject {
    param([string]$Source, [string]$ObjectRoot)
    $name = ($Source -replace '[\\/:.]', '_') + '.o'
    $object = Join-Path $ObjectRoot $name
    & $gcc @cFlags -c $Source -o $object
    if ($LASTEXITCODE -ne 0) { throw "Falha ao compilar $Source" }
    return $object
}

function Compile-CppObject {
    param([string]$Source, [string]$ObjectRoot)
    $name = ($Source -replace '[\\/:.]', '_') + '.o'
    $object = Join-Path $ObjectRoot $name
    & $gxx @cppFlags -c $Source -o $object
    if ($LASTEXITCODE -ne 0) { throw "Falha ao compilar $Source" }
    return $object
}

$minHookSources = @(
    (Join-Path $menuProject 'vendor\minhook\src\buffer.c'),
    (Join-Path $menuProject 'vendor\minhook\src\hook.c'),
    (Join-Path $menuProject 'vendor\minhook\src\trampoline.c'),
    (Join-Path $menuProject 'vendor\minhook\src\hde\hde64.c')
)

$menuObjects = [System.Collections.Generic.List[string]]::new()
foreach ($source in $minHookSources) { $menuObjects.Add((Compile-CObject $source $menuObjectRoot)) }
$menuObjects.Add((Compile-CppObject (Join-Path $menuProject 'mod_menu_overlay.cpp') $menuObjectRoot))
foreach ($source in @(
    'vendor\imgui\imgui.cpp',
    'vendor\imgui\imgui_draw.cpp',
    'vendor\imgui\imgui_tables.cpp',
    'vendor\imgui\imgui_widgets.cpp',
    'vendor\imgui\backends\imgui_impl_dx12.cpp'
)) {
    $menuObjects.Add((Compile-CppObject (Join-Path $menuProject $source) $menuObjectRoot))
}
$menuOutput = Join-Path $buildRoot 'mod_menu.dll'
& $gxx -shared -static-libgcc -static-libstdc++ -o $menuOutput @menuObjects `
    -ld3d12 -ldxgi -ld3dcompiler -ldxguid -lxinput9_1_0 -luser32 -lkernel32
if ($LASTEXITCODE -ne 0) { throw 'Falha ao vincular mod_menu.dll' }

$loaderObjects = @(
    (Compile-CppObject (Join-Path $loaderProject 'dxgi_proxy.cpp') $loaderObjectRoot),
    (Compile-CppObject (Join-Path $loaderProject 'mod_loader.cpp') $loaderObjectRoot)
)
$loaderOutput = Join-Path $buildRoot 'dxgi.dll'
& $gxx -shared -static-libgcc -static-libstdc++ -o $loaderOutput @loaderObjects -luser32 -lkernel32
if ($LASTEXITCODE -ne 0) { throw 'Falha ao vincular dxgi.dll' }

$validatorObject = Compile-CppObject (Join-Path $loaderProject 'validate_main.cpp') $loaderObjectRoot
$validatorOutput = Join-Path $buildRoot 'mod_loader_validate.exe'
& $gxx -municode -static-libgcc -static-libstdc++ -o $validatorOutput $validatorObject $loaderObjects[1] -luser32 -lkernel32
if ($LASTEXITCODE -ne 0) { throw 'Falha ao vincular mod_loader_validate.exe' }

$smokeRoot = Join-Path $buildRoot 'smoke-runtime'
New-Item -ItemType Directory -Force (Join-Path $smokeRoot 'mods\mod_menu') | Out-Null
$smokeOutput = Join-Path $smokeRoot 'mod_loader_proxy_smoke.exe'
& $gxx @cppFlags -static -o $smokeOutput (Join-Path $loaderProject 'proxy_smoke.cpp') -lole32 -lkernel32
if ($LASTEXITCODE -ne 0) { throw 'Falha ao vincular mod_loader_proxy_smoke.exe' }

$pluginMinHookObjects = [System.Collections.Generic.List[string]]::new()
foreach ($source in $minHookSources) { $pluginMinHookObjects.Add((Compile-CObject $source $pluginObjectRoot)) }

$itemObject = Compile-CppObject (Join-Path $gameRoot 'mods\item_world\item_world.cpp') $pluginObjectRoot
$itemWorldOutput = Join-Path $buildRoot 'item_world.dll'
& $gxx -shared -static-libgcc -static-libstdc++ -o $itemWorldOutput $itemObject @pluginMinHookObjects -lkernel32
if ($LASTEXITCODE -ne 0) { throw 'Falha ao vincular item_world.dll' }

$charaObject = Compile-CppObject (Join-Path $gameRoot 'mods\chara_world\chara_world.cpp') $pluginObjectRoot
$charaWorldOutput = Join-Path $buildRoot 'chara_world.dll'
& $gxx -shared -static-libgcc -static-libstdc++ -o $charaWorldOutput $charaObject @pluginMinHookObjects -lkernel32
if ($LASTEXITCODE -ne 0) { throw 'Falha ao vincular chara_world.dll' }

$safeBackupOutput = Join-Path $buildRoot 'safe_backup.dll'
& $gxx @cppFlags -shared -static-libgcc -static-libstdc++ -o $safeBackupOutput `
    (Join-Path $gameRoot 'mods\safe_backup\safe_backup.cpp') -lshell32 -lkernel32
if ($LASTEXITCODE -ne 0) { throw 'Falha ao vincular safe_backup.dll' }

$modMenuInstallerOutput = Join-Path $buildRoot 'INSTALAR_MOD_MENU.exe'
& $gxx @cppFlags -static -o $modMenuInstallerOutput `
    (Join-Path $gameRoot 'mods\mod_menu\INSTALAR_MOD_MENU.cpp') -lkernel32
if ($LASTEXITCODE -ne 0) { throw 'Falha ao vincular INSTALAR_MOD_MENU.exe' }

$cheatShopOutput = Join-Path $buildRoot 'APLICAR_MOD_CHEAT_SHOP.exe'
& $gxx @cppFlags -municode -static -o $cheatShopOutput `
    (Join-Path $gameRoot 'mods\cheat_shop\apply_cheat_shop.cpp') -lkernel32
if ($LASTEXITCODE -ne 0) { throw 'Falha ao vincular APLICAR_MOD_CHEAT_SHOP.exe' }

$darkAssemblyOutput = Join-Path $buildRoot 'APLICAR_MOD_DARK_ASSEMBLY.exe'
& $gxx @cppFlags -static -o $darkAssemblyOutput `
    (Join-Path $gameRoot 'mods\dark_assembly\apply_dark_assembly.cpp') -lkernel32
if ($LASTEXITCODE -ne 0) { throw 'Falha ao vincular APLICAR_MOD_DARK_ASSEMBLY.exe' }

$dlcUnlockerOutput = Join-Path $buildRoot 'APLICAR_MOD_DLC.exe'
& $gxx @cppFlags -static -o $dlcUnlockerOutput `
    (Join-Path $gameRoot 'mods\dlc_unlocker\apply_dlc_unlocker.cpp') -lkernel32
if ($LASTEXITCODE -ne 0) { throw 'Falha ao vincular APLICAR_MOD_DLC.exe' }

$target = $gameRoot.ToString()
$deployments = @(
    @($loaderOutput, (Join-Path $target 'dxgi.dll')),
    @($menuOutput, (Join-Path $target 'mods\mod_menu\mod_menu.dll')),
    @($itemWorldOutput, (Join-Path $target 'mods\item_world\item_world.dll')),
    @($charaWorldOutput, (Join-Path $target 'mods\chara_world\chara_world.dll')),
    @($safeBackupOutput, (Join-Path $target 'mods\safe_backup\safe_backup.dll')),
    @($modMenuInstallerOutput, (Join-Path $target 'mods\mod_menu\INSTALAR_MOD_MENU.exe')),
    @($cheatShopOutput, (Join-Path $target 'mods\cheat_shop\APLICAR_MOD_CHEAT_SHOP.exe')),
    @($darkAssemblyOutput, (Join-Path $target 'mods\dark_assembly\APLICAR_MOD_DARK_ASSEMBLY.exe')),
    @($dlcUnlockerOutput, (Join-Path $target 'mods\dlc_unlocker\APLICAR_MOD_DLC.exe'))
)
foreach ($deployment in $deployments) {
    try {
        New-Item -ItemType Directory -Force (Split-Path -Parent $deployment[1]) | Out-Null
        Copy-Item -LiteralPath $deployment[0] -Destination $deployment[1] -Force
        Write-Host "[OK] Atualizado: $($deployment[1])"
    } catch {
        throw "Falha ao implantar $($deployment[1]). Feche o jogo e tente novamente. $($_.Exception.Message)"
    }
}

& $validatorOutput ($gameRoot.ToString())
if ($LASTEXITCODE -ne 0) { throw "Validador do Mod Loader falhou com codigo $LASTEXITCODE" }

Copy-Item -LiteralPath $loaderOutput -Destination (Join-Path $smokeRoot 'dxgi.dll') -Force
Copy-Item -LiteralPath (Join-Path $gameRoot 'NmplDLL.dll') `
    -Destination (Join-Path $smokeRoot 'NmplDLL.dll') -Force
Copy-Item -LiteralPath $menuOutput -Destination (Join-Path $smokeRoot 'mods\mod_menu\mod_menu.dll') -Force
Copy-Item -LiteralPath (Join-Path $gameRoot 'mods\mod_menu\mod.json') `
    -Destination (Join-Path $smokeRoot 'mods\mod_menu\mod.json') -Force
Copy-Item -LiteralPath (Join-Path $gameRoot 'mods\mod_menu\enabled.txt') `
    -Destination (Join-Path $smokeRoot 'mods\mod_menu\enabled.txt') -Force
Copy-Item -LiteralPath (Join-Path $gameRoot 'mods\mod_menu\config.json') `
    -Destination (Join-Path $smokeRoot 'mods\mod_menu\config.json') -Force
& $smokeOutput
if ($LASTEXITCODE -ne 0) { throw "Smoke test do proxy/loader falhou com codigo $LASTEXITCODE" }
$smokeLog = Get-Content -Raw (Join-Path $smokeRoot 'mods\mod_loader.log')
if ($smokeLog -notmatch 'bootstrap concluido com 1 manifesto') {
    throw 'Smoke test nao confirmou o bootstrap isolado do system mod.'
}
Write-Host '[OK] Smoke test isolado do proxy/loader concluido.'

Write-Host '[OK] Loader, Mod Menu e plugins ABI v1 compilados com sucesso.'
