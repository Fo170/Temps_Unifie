# Changelog

## [1.0.0] - 2026-09-25
### Ajouté
- Publication en **librairie header-only** `Temps_Unifie` (un seul `src/Temps_Unifie.h`, sans `.cpp`) pour **ESP8266 et ESP32**.
- Accès au temps via le **SNTP natif** (`configTzTime()`, `time()`/`localtime_r()`/`gmtime_r()`) : **aucune dépendance externe** (ni `NTPClient`, ni `WiFiUdp`, ni `TimeLib`), gain flash ~2,8 Ko et suppression du socket UDP / second client NTP.
- **Multi-plateformes** : sélection interne automatique — ESP32 `esp_sntp.h` + `sntp_set_time_sync_notification_cb()` ; ESP8266 lwIP (`sntp.h`, `coredecls.h`) + `settimeofday_cb()`. L'API publique est identique.
- **Librairie multi-pays** : `TIME_Init(const char* fuseau, const char* serveur)` prend désormais le fuseau POSIX et le serveur NTP en paramètres et appelle lui-même `configTzTime()` (plus besoin de l'appeler avant). Getters `Temps_Serveur()` / `Temps_Fuseau()`.
- API : `TIME_Init()`, `TIME_Maj()`, `Temps_UTC()`, `Temps_Synchronise()`, `Temps_Serveur()`, `Temps_Fuseau()`, `Temps_DerniereSync_ms()`, `Temps_AgeSync_s()`, `Temps_Heure_UTC()`, `Temps_Date_UTC()`, `Temps_Heure_LOCAL()`, `Temps_Date_LOCAL()`.
- **Valeurs numériques brutes** : variables globales `t_UTC` et `t_LOCAL` (`time_t`), rafraîchies par `tempsMajVariables()` appelée dans `TIME_Init()`, `TIME_Maj()` **et chaque fonction d'accès** (`Temps_UTC()`, `Temps_Synchronise()`, helpers String) : toute lecture renvoie donc un temps exact et cohérent. `t_LOCAL` est l'heure « murale » locale, à décomposer avec `gmtime_r()` pour accéder aux champs date/heure sans passer par les `String`.
- Diagnostic SNTP : `NTP_FCT_Actif()`, `NTP_FCT_ServeurReel()`, `NTP_FCT_ModeTexte()`, `NTP_FCT_StatutSntpTexte()`, `NTP_FCT_Intervalle_ms()`, `NTP_FCT_SetIntervalle_ms()`, `NTP_FCT_ForcerSync()` (intervalle/mode/statut complets sur ESP32 ; dégradés sur ESP8266, non exposés par le core).
- Publication PlatformIO/Arduino : `library.json`, `library.properties`, `keywords.txt`, `LICENSE` (GPL-3.0-only), `.gitignore`.
- Exemple PlatformIO (`exemple/`, envs `esp12e` et `esp32dev`) : connexion Wi-Fi, `TIME_Init(fuseau, serveur)` puis affichage périodique UTC/local.

### Modifié (rupture d'API par rapport au fork initial)
- `Temps_Heure_FR()` renommée en **`Temps_Heure_LOCAL()`** et `Temps_Date_FR()` en **`Temps_Date_LOCAL()`** (multi-pays).
- `TIME_Init()` prend désormais **deux paramètres** (`fuseau`, `serveur`) et **démarre le SNTP** en interne.

### Notes
- Fork de l'ancien `Temps_Unifie.h` (`NTPClient` + `TimeLib`, ESP8266 + ESP32), qui n'est pas modifié.
- Le fuseau (CET/CEST, etc.) est géré par le TZ POSIX de `configTzTime()` : aucun calcul manuel d'heure d'été.
- Sur ESP32, `TIME_Init()` applique un intervalle de re-sync de 60 s (le défaut ESP-IDF est de 3 h) via `sntp_restart()`. Sur ESP8266, l'intervalle lwIP n'est pas exposé par le core.
