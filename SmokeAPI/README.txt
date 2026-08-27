THIRD-PARTY COMPONENT NOTICE
============================

SmokeAPI is a third-party project created and maintained by acidicoala.
It is not authored or maintained by the Disgaea Mayhem Mod Loader project.

Official project:
https://github.com/acidicoala/SmokeAPI

SmokeAPI is distributed under The Unlicense. See the upstream repository for the original source code, license text, releases, documentation, and current usage warnings.

The SmokeAPI binaries included with this mod package are redistributed as a third-party dependency used by the DLC Unlocker integration.

Use of SmokeAPI or similar unlocker functionality may be subject to the terms and policies of Steam and/or the affected game. Users are responsible for deciding whether to use this functionality.

Project page: https://github.com/acidicoala/SmokeAPI#readme
Forum topic: https://cs.rin.ru/forum/viewtopic.php?p=2597932#p2597932
DLC Database: https://steamdb.info/

*** UPSTREAM NOTE ***

Do NOT use the SmokeAPI.config.json file unless you have a good reason.
The default config file enables logging, which might have a negative impact on performance in games.
So, unless you really need to see logs for debugging issues, it is advised to either:
  disable logging in the config file by setting the "logging" field to false
or
  not use the SmokeAPI.config.json file at all.

This mod package intentionally provides its own SmokeAPI.config.json because the DLC Unlocker integration requires specific inventory entries. Logging is disabled in the provided configuration.
