$ErrorActionPreference = 'Stop'

$menuProject = Split-Path -Parent $MyInvocation.MyCommand.Path
$nativeRoot = Resolve-Path (Join-Path $menuProject '..')
$gameRoot = Resolve-Path (Join-Path $nativeRoot '..')
$loaderProject = Join-Path $nativeRoot 'mod_loader'
$buildRoot = Join-Path $menuProject 'build'
$menuObjectRoot = Join-Path $buildRoot 'obj-menu'
$loaderObjectRoot = Join-Path $buildRoot 'obj-loader'
$pluginObjectRoot = Join-Path $buildRoot 'obj-plugins'
$gxxCommand = Get-Command g++.exe -ErrorAction SilentlyContinue
if ($null -ne $gxxCommand) {
    $gxx = $gxxCommand.Source
} else {
    $gxxCandidates = @(Get-PSDrive -PSProvider FileSystem | ForEach-Object {
        Join-Path $_.Root 'TDM-GCC-64\bin\g++.exe'
    } | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf })
    if ($gxxCandidates.Count -ne 1) {
        throw 'g++.exe nao foi encontrado no PATH nem em uma unica instalacao TDM-GCC-64 dos drives montados.'
    }
    $gxx = $gxxCandidates[0]
}
$gcc = Join-Path (Split-Path -Parent $gxx) 'gcc.exe'
if (-not (Test-Path -LiteralPath $gcc -PathType Leaf)) {
    throw "gcc.exe obrigatorio ausente ao lado de g++.exe: $gcc"
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

$loaderMinHookObjects = [System.Collections.Generic.List[string]]::new()
foreach ($source in $minHookSources) { $loaderMinHookObjects.Add((Compile-CObject $source $loaderObjectRoot)) }

$loaderObjects = @(
    (Compile-CppObject (Join-Path $loaderProject 'dxgi_proxy.cpp') $loaderObjectRoot),
    (Compile-CppObject (Join-Path $loaderProject 'mod_loader.cpp') $loaderObjectRoot)
)
$loaderOutput = Join-Path $buildRoot 'dxgi.dll'
& $gxx -shared -static-libgcc -static-libstdc++ -o $loaderOutput @loaderObjects @loaderMinHookObjects -luser32 -lkernel32
if ($LASTEXITCODE -ne 0) { throw 'Falha ao vincular dxgi.dll' }

$validatorObject = Compile-CppObject (Join-Path $loaderProject 'validate_main.cpp') $loaderObjectRoot
$validatorOutput = Join-Path $buildRoot 'mod_loader_validate.exe'
& $gxx -municode -static-libgcc -static-libstdc++ -o $validatorOutput $validatorObject $loaderObjects[1] @loaderMinHookObjects -luser32 -lkernel32
if ($LASTEXITCODE -ne 0) { throw 'Falha ao vincular mod_loader_validate.exe' }

$smokeRoot = Join-Path $buildRoot 'smoke-runtime'
New-Item -ItemType Directory -Force (Join-Path $smokeRoot 'mods\mod_menu') | Out-Null
$smokeOutput = Join-Path $smokeRoot 'mod_loader_proxy_smoke.exe'
& $gxx @cppFlags -static -o $smokeOutput (Join-Path $loaderProject 'proxy_smoke.cpp') -lole32 -lkernel32
if ($LASTEXITCODE -ne 0) { throw 'Falha ao vincular mod_loader_proxy_smoke.exe' }

$itemObject = Compile-CppObject (Join-Path $gameRoot 'mods\item_world\item_world.cpp') $pluginObjectRoot
$itemWorldOutput = Join-Path $buildRoot 'item_world.dll'
& $gxx -shared -static-libgcc -static-libstdc++ -o $itemWorldOutput $itemObject -lkernel32
if ($LASTEXITCODE -ne 0) { throw 'Falha ao vincular item_world.dll' }

$charaObject = Compile-CppObject (Join-Path $gameRoot 'mods\chara_world\chara_world.cpp') $pluginObjectRoot
$charaWorldOutput = Join-Path $buildRoot 'chara_world.dll'
& $gxx -shared -static-libgcc -static-libstdc++ -o $charaWorldOutput $charaObject -lkernel32
if ($LASTEXITCODE -ne 0) { throw 'Falha ao vincular chara_world.dll' }

$safeBackupOutput = Join-Path $buildRoot 'safe_backup.dll'
& $gxx @cppFlags -shared -static-libgcc -static-libstdc++ -o $safeBackupOutput `
    (Join-Path $gameRoot 'mods\safe_backup\safe_backup.cpp') -lshell32 -lkernel32
if ($LASTEXITCODE -ne 0) { throw 'Falha ao vincular safe_backup.dll' }

$modMenuInstallerOutput = Join-Path $buildRoot 'INSTALAR_MOD_MENU.exe'
& $gxx @cppFlags -static -o $modMenuInstallerOutput `
    (Join-Path $gameRoot 'mods\mod_menu\INSTALAR_MOD_MENU.cpp') -lkernel32
if ($LASTEXITCODE -ne 0) { throw 'Falha ao vincular INSTALAR_MOD_MENU.exe' }

$suiteInstallerOutput = Join-Path $buildRoot 'INSTALAR_MOD.exe'
& $gxx @cppFlags -municode -static -o $suiteInstallerOutput `
    (Join-Path $gameRoot 'INSTALAR_MOD.cpp') -lkernel32
if ($LASTEXITCODE -ne 0) { throw 'Falha ao vincular INSTALAR_MOD.exe' }

$cheatShopObject = Compile-CppObject `
    (Join-Path $gameRoot 'mods\cheat_shop\cheat_shop.cpp') $pluginObjectRoot
$cheatShopOutput = Join-Path $buildRoot 'cheat_shop.dll'
& $gxx -shared -static-libgcc -static-libstdc++ -o $cheatShopOutput `
    $cheatShopObject -lkernel32
if ($LASTEXITCODE -ne 0) { throw 'Falha ao vincular cheat_shop.dll' }

$darkAssemblyObject = Compile-CppObject `
    (Join-Path $gameRoot 'mods\dark_assembly\dark_assembly.cpp') $pluginObjectRoot
$darkAssemblyOutput = Join-Path $buildRoot 'dark_assembly.dll'
& $gxx -shared -static-libgcc -static-libstdc++ -o $darkAssemblyOutput `
    $darkAssemblyObject -lkernel32
if ($LASTEXITCODE -ne 0) { throw 'Falha ao vincular dark_assembly.dll' }

$dlcUnlockerObject = Compile-CppObject `
    (Join-Path $gameRoot 'mods\dlc_unlocker\dlc_unlocker.cpp') $pluginObjectRoot
$dlcUnlockerOutput = Join-Path $buildRoot 'dlc_unlocker.dll'
& $gxx -shared -static-libgcc -static-libstdc++ -o $dlcUnlockerOutput `
    $dlcUnlockerObject -lkernel32
if ($LASTEXITCODE -ne 0) { throw 'Falha ao vincular dlc_unlocker.dll' }

$target = $gameRoot.ToString()
$deployments = @(
    @($loaderOutput, (Join-Path $target 'dxgi.dll')),
    @($menuOutput, (Join-Path $target 'mods\mod_menu\mod_menu.dll')),
    @($itemWorldOutput, (Join-Path $target 'mods\item_world\item_world.dll')),
    @($charaWorldOutput, (Join-Path $target 'mods\chara_world\chara_world.dll')),
    @($safeBackupOutput, (Join-Path $target 'mods\safe_backup\safe_backup.dll')),
    @($modMenuInstallerOutput, (Join-Path $target 'mods\mod_menu\INSTALAR_MOD_MENU.exe')),
    @($suiteInstallerOutput, (Join-Path $target 'INSTALAR_MOD.exe')),
    @($validatorOutput, (Join-Path $target 'tools\mod_loader_validate.exe')),
    @($cheatShopOutput, (Join-Path $target 'mods\cheat_shop\cheat_shop.dll')),
    @($darkAssemblyOutput, (Join-Path $target 'mods\dark_assembly\dark_assembly.dll')),
    @($dlcUnlockerOutput, (Join-Path $target 'mods\dlc_unlocker\dlc_unlocker.dll'))
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

$releaseRoot = Join-Path $buildRoot 'nexus-package'
$installerTestRoot = Join-Path $buildRoot 'installer-test'
$installerRollbackRoot = Join-Path $buildRoot 'installer-rollback-test'
New-Item -ItemType Directory -Force `
    $releaseRoot, $installerTestRoot, $installerRollbackRoot | Out-Null

function Copy-ReleaseFile {
    param([string]$Source, [string]$RelativePath)
    if (-not (Test-Path -LiteralPath $Source -PathType Leaf)) {
        throw "Arquivo do pacote ausente: $Source"
    }
    $destination = Join-Path $releaseRoot $RelativePath
    New-Item -ItemType Directory -Force (Split-Path -Parent $destination) | Out-Null
    Copy-Item -LiteralPath $Source -Destination $destination -Force
}

$releaseFiles = @(
    @($loaderOutput, 'dxgi.dll'),
    @($suiteInstallerOutput, 'INSTALAR_MOD.exe'),
    @($validatorOutput, 'tools\mod_loader_validate.exe'),
    @((Join-Path $gameRoot 'tools\INSTRUCOES_RESGATES.txt'), 'tools\INSTRUCOES_RESGATES.txt'),
    @((Join-Path $gameRoot 'README_NEXUS.txt'), 'README.txt'),
    @((Join-Path $gameRoot 'SmokeAPI.config.json'), 'SmokeAPI.config.json'),
    @((Join-Path $gameRoot 'SmokeAPI\smoke_api64.dll'), 'SmokeAPI\smoke_api64.dll'),
    @((Join-Path $gameRoot 'SmokeAPI\README.txt'), 'SmokeAPI\README.txt'),

    @($menuOutput, 'mods\mod_menu\mod_menu.dll'),
    @((Join-Path $gameRoot 'mods\mod_menu\mod.json'), 'mods\mod_menu\mod.json'),
    @((Join-Path $gameRoot 'mods\mod_menu\config.json'), 'mods\mod_menu\config.json'),
    @((Join-Path $gameRoot 'mods\mod_menu\enabled.txt'), 'mods\mod_menu\enabled.txt'),
    @((Join-Path $gameRoot 'mods\mod_menu\README.md'), 'mods\mod_menu\README.md'),
    @((Join-Path $gameRoot 'mods\mod_menu\main_menu\mods_slot.dds'), 'mods\mod_menu\main_menu\mods_slot.dds'),
    @((Join-Path $gameRoot 'mods\mod_menu\main_menu\OFL.txt'), 'mods\mod_menu\main_menu\OFL.txt'),

    @($charaWorldOutput, 'mods\chara_world\chara_world.dll'),
    @((Join-Path $gameRoot 'mods\chara_world\mod.json'), 'mods\chara_world\mod.json'),
    @((Join-Path $gameRoot 'mods\chara_world\config.json'), 'mods\chara_world\config.json'),
    @((Join-Path $gameRoot 'mods\chara_world\enabled.txt'), 'mods\chara_world\enabled.txt'),
    @((Join-Path $gameRoot 'mods\chara_world\README.md'), 'mods\chara_world\README.md'),

    @($itemWorldOutput, 'mods\item_world\item_world.dll'),
    @((Join-Path $gameRoot 'mods\item_world\mod.json'), 'mods\item_world\mod.json'),
    @((Join-Path $gameRoot 'mods\item_world\config.json'), 'mods\item_world\config.json'),
    @((Join-Path $gameRoot 'mods\item_world\enabled.txt'), 'mods\item_world\enabled.txt'),
    @((Join-Path $gameRoot 'mods\item_world\README.md'), 'mods\item_world\README.md'),

    @($cheatShopOutput, 'mods\cheat_shop\cheat_shop.dll'),
    @((Join-Path $gameRoot 'mods\cheat_shop\mod.json'), 'mods\cheat_shop\mod.json'),
    @((Join-Path $gameRoot 'mods\cheat_shop\config.json'), 'mods\cheat_shop\config.json'),
    @((Join-Path $gameRoot 'mods\cheat_shop\enabled.txt'), 'mods\cheat_shop\enabled.txt'),
    @((Join-Path $gameRoot 'mods\cheat_shop\README.md'), 'mods\cheat_shop\README.md'),

    @($darkAssemblyOutput, 'mods\dark_assembly\dark_assembly.dll'),
    @((Join-Path $gameRoot 'mods\dark_assembly\mod.json'), 'mods\dark_assembly\mod.json'),
    @((Join-Path $gameRoot 'mods\dark_assembly\config.json'), 'mods\dark_assembly\config.json'),
    @((Join-Path $gameRoot 'mods\dark_assembly\enabled.txt'), 'mods\dark_assembly\enabled.txt'),
    @((Join-Path $gameRoot 'mods\dark_assembly\README.md'), 'mods\dark_assembly\README.md'),

    @($dlcUnlockerOutput, 'mods\dlc_unlocker\dlc_unlocker.dll'),
    @((Join-Path $gameRoot 'mods\dlc_unlocker\mod.json'), 'mods\dlc_unlocker\mod.json'),
    @((Join-Path $gameRoot 'mods\dlc_unlocker\config.json'), 'mods\dlc_unlocker\config.json'),
    @((Join-Path $gameRoot 'mods\dlc_unlocker\enabled.txt'), 'mods\dlc_unlocker\enabled.txt'),
    @((Join-Path $gameRoot 'mods\dlc_unlocker\README.md'), 'mods\dlc_unlocker\README.md'),

    @($safeBackupOutput, 'mods\safe_backup\safe_backup.dll'),
    @((Join-Path $gameRoot 'mods\safe_backup\mod.json'), 'mods\safe_backup\mod.json'),
    @((Join-Path $gameRoot 'mods\safe_backup\config.json'), 'mods\safe_backup\config.json'),
    @((Join-Path $gameRoot 'mods\safe_backup\enabled.txt'), 'mods\safe_backup\enabled.txt'),
    @((Join-Path $gameRoot 'mods\safe_backup\README.md'), 'mods\safe_backup\README.md')
)
foreach ($releaseFile in $releaseFiles) {
    Copy-ReleaseFile -Source $releaseFile[0] -RelativePath $releaseFile[1]
}

$utf8NoBom = [System.Text.UTF8Encoding]::new($false)
$disabledMods = @('chara_world', 'item_world', 'cheat_shop', 'dark_assembly', 'dlc_unlocker', 'safe_backup')
foreach ($modId in $disabledMods) {
    [System.IO.File]::WriteAllText(
        (Join-Path $releaseRoot "mods\$modId\enabled.txt"), '0', $utf8NoBom)
}
[System.IO.File]::WriteAllText(
    (Join-Path $releaseRoot 'mods\mod_menu\enabled.txt'), '1', $utf8NoBom)
[System.IO.File]::WriteAllText(
    (Join-Path $releaseRoot 'mods\chara_world\config.json'),
    @'
{
  "schema_version": 1,
  "mod_id": "chara_world",
  "options": {
    "locked_energy": 100,
    "freeze_energy": true,
    "tile_status_multiplier": 1.0
  }
}
'@, $utf8NoBom)
[System.IO.File]::WriteAllText(
    (Join-Path $releaseRoot 'mods\item_world\config.json'),
    @'
{
  "schema_version": 1,
  "mod_id": "item_world",
  "options": {
    "level_exp_enabled": false,
    "level_exp_multiplier": 1.0,
    "item_points_enabled": false,
    "item_point_multiplier": 1.0,
    "rarity_enabled": false,
    "rarity_global": false,
    "minimum_rarity": 50
  }
}
'@, $utf8NoBom)

& $validatorOutput $releaseRoot
if ($LASTEXITCODE -ne 0) { throw "Pacote Nexus invalido (codigo $LASTEXITCODE)" }
$releaseLog = Join-Path $releaseRoot 'mods\mod_loader.log'
if (Test-Path -LiteralPath $releaseLog) { Remove-Item -LiteralPath $releaseLog -Force }

New-Item -ItemType File -Force (Join-Path $installerTestRoot 'Disgaea_Mayhem.exe') | Out-Null
Copy-Item -LiteralPath (Join-Path $gameRoot 'NmplDLL.dll') `
    -Destination (Join-Path $installerTestRoot 'NmplDLL.dll') -Force
Copy-Item -LiteralPath (Join-Path $gameRoot 'steam_api64_o.dll') `
    -Destination (Join-Path $installerTestRoot 'steam_api64.dll') -Force
New-Item -ItemType Directory -Force `
    (Join-Path $installerTestRoot 'mods\native'), `
    (Join-Path $installerTestRoot 'mods\main_menu') | Out-Null
[System.IO.File]::WriteAllText(
    (Join-Path $installerTestRoot 'mods\registry.json'), '{}', $utf8NoBom)
[System.IO.File]::WriteAllText(
    (Join-Path $installerTestRoot 'mods\native\DisgaeaMayhemModMenu.dll'), 'obsolete', $utf8NoBom)
[System.IO.File]::WriteAllText(
    (Join-Path $installerTestRoot 'mods\main_menu\OFL.txt'), 'obsolete', $utf8NoBom)

& (Join-Path $releaseRoot 'INSTALAR_MOD.exe') $installerTestRoot
if ($LASTEXITCODE -ne 0) { throw "Teste isolado do instalador falhou (codigo $LASTEXITCODE)" }
foreach ($obsolete in @(
    'mods\registry.json',
    'mods\native\DisgaeaMayhemModMenu.dll',
    'mods\main_menu\OFL.txt'
)) {
    if (Test-Path -LiteralPath (Join-Path $installerTestRoot $obsolete)) {
        throw "Instalador preservou artefato obsoleto: $obsolete"
    }
}
[System.IO.File]::WriteAllText(
    (Join-Path $installerTestRoot 'mods\item_world\enabled.txt'), '1', $utf8NoBom)
$testConfigPath = Join-Path $installerTestRoot 'mods\item_world\config.json'
$testConfig = Get-Content -LiteralPath $testConfigPath -Raw | ConvertFrom-Json
$testConfig.options.minimum_rarity = 77
[System.IO.File]::WriteAllText(
    $testConfigPath, ($testConfig | ConvertTo-Json -Depth 8), $utf8NoBom)
& (Join-Path $releaseRoot 'INSTALAR_MOD.exe') $installerTestRoot
if ($LASTEXITCODE -ne 0) { throw "Teste de atualizacao do instalador falhou (codigo $LASTEXITCODE)" }
if ((Get-Content -LiteralPath (Join-Path $installerTestRoot 'mods\item_world\enabled.txt') -Raw).Trim() -ne '1') {
    throw 'Instalador nao preservou enabled.txt durante a atualizacao.'
}
$preservedConfig = Get-Content -LiteralPath $testConfigPath -Raw | ConvertFrom-Json
if ($preservedConfig.options.minimum_rarity -ne 77) {
    throw 'Instalador nao preservou config.json durante a atualizacao.'
}
Write-Host '[OK] Instalacao limpa e atualizacao com configuracao preservada validadas.'

New-Item -ItemType File -Force `
    (Join-Path $installerRollbackRoot 'Disgaea_Mayhem.exe') | Out-Null
Copy-Item -LiteralPath (Join-Path $gameRoot 'NmplDLL.dll') `
    -Destination (Join-Path $installerRollbackRoot 'NmplDLL.dll') -Force
Copy-Item -LiteralPath (Join-Path $gameRoot 'steam_api64_o.dll') `
    -Destination (Join-Path $installerRollbackRoot 'steam_api64.dll') -Force
New-Item -ItemType Directory -Force `
    (Join-Path $installerRollbackRoot 'mods\item_world'), `
    (Join-Path $installerRollbackRoot 'mods\native') | Out-Null
[System.IO.File]::WriteAllText(
    (Join-Path $installerRollbackRoot 'dxgi.dll'), 'loader-anterior', $utf8NoBom)
[System.IO.File]::WriteAllText(
    (Join-Path $installerRollbackRoot 'mods\item_world\config.json'),
    '{"schema_version":0}', $utf8NoBom)
[System.IO.File]::WriteAllText(
    (Join-Path $installerRollbackRoot 'mods\native\DisgaeaMayhemModMenu.dll'),
    'overlay-anterior', $utf8NoBom)
& (Join-Path $releaseRoot 'INSTALAR_MOD.exe') $installerRollbackRoot
if ($LASTEXITCODE -eq 0) {
    throw 'Teste de rollback deveria falhar por config.json invalido.'
}
if ((Get-Content -LiteralPath (Join-Path $installerRollbackRoot 'dxgi.dll') -Raw) -ne 'loader-anterior') {
    throw 'Rollback nao restaurou o loader anterior.'
}
if (-not (Test-Path -LiteralPath `
        (Join-Path $installerRollbackRoot 'mods\native\DisgaeaMayhemModMenu.dll'))) {
    throw 'Rollback nao restaurou o artefato removido.'
}
if (Test-Path -LiteralPath (Join-Path $installerRollbackRoot 'steam_api64_o.dll')) {
    throw 'Rollback preservou uma copia steam_api64_o.dll que nao existia antes.'
}
$originalSteamHash = (Get-FileHash -Algorithm SHA256 `
    -LiteralPath (Join-Path $gameRoot 'steam_api64_o.dll')).Hash
$rollbackSteamHash = (Get-FileHash -Algorithm SHA256 `
    -LiteralPath (Join-Path $installerRollbackRoot 'steam_api64.dll')).Hash
if ($originalSteamHash -ne $rollbackSteamHash) {
    throw 'Rollback nao restaurou steam_api64.dll.'
}
$rollbackTransactions = @(Get-ChildItem -LiteralPath $installerRollbackRoot -Force | `
    Where-Object { $_.Name -like '.dm_mod_install_transaction_*' })
if ($rollbackTransactions.Count -ne 0) {
    throw 'Rollback deixou uma transacao temporaria.'
}
Write-Host '[OK] Falha forcada restaurou integralmente o estado anterior.'

$packageCandidate = Join-Path $buildRoot 'Disgaea_Mayhem_Mod_Loader_Nexus.new.zip'
$packageOutput = Join-Path $gameRoot 'Disgaea_Mayhem_Mod_Loader_Nexus.zip'
Compress-Archive -Path ((Get-ChildItem -LiteralPath $releaseRoot).FullName) `
    -DestinationPath $packageCandidate -CompressionLevel Optimal -Force
Add-Type -AssemblyName System.IO.Compression.FileSystem
$archive = [System.IO.Compression.ZipFile]::OpenRead($packageCandidate)
try {
    $actualEntries = @($archive.Entries | Where-Object { $_.Name } | ForEach-Object {
        $_.FullName.Replace('/', '\')
    } | Sort-Object)
} finally {
    $archive.Dispose()
}
$expectedEntries = @($releaseFiles | ForEach-Object { $_[1] } | Sort-Object)
$entryDifference = @(Compare-Object $expectedEntries $actualEntries)
if ($entryDifference.Count -ne 0) {
    $entryDifference | Out-String | Write-Host
    throw 'Conteudo do ZIP diverge da lista canonica.'
}
Copy-Item -LiteralPath $packageCandidate -Destination $packageOutput -Force
Write-Host "[OK] Pacote Nexus validado e atualizado: $packageOutput"

Write-Host '[OK] Loader, Mod Menu, plugins ABI v2, instalador e pacote compilados com sucesso.'

$resolvedBuildRoot = [System.IO.Path]::GetFullPath($buildRoot)
$expectedBuildRoot = [System.IO.Path]::GetFullPath((Join-Path $menuProject 'build'))
if (-not [System.StringComparer]::OrdinalIgnoreCase.Equals(
        $resolvedBuildRoot, $expectedBuildRoot)) {
    throw "Diretorio de compilacao inesperado: $resolvedBuildRoot"
}
Remove-Item -LiteralPath $resolvedBuildRoot -Recurse -Force
Write-Host '[OK] Arquivos temporarios de compilacao removidos.'
