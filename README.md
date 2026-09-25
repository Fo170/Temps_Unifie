# Temps_Unifie — Gestion du temps unifiée pour ESP8266 et ESP32 (SNTP natif, multi-pays)

*Version 1.0.0 — GPL-3.0-only — Auteur : Olivier FOURNET (Fo170)*

Librairie **header-only** (Arduino / PlatformIO) pour **ESP8266 et ESP32** : accès au temps basé sur le **SNTP natif** (lwIP du core ESP8266 / ESP-IDF de l'ESP32), **sans aucune dépendance externe** (ni `NTPClient`, ni `WiFiUdp`, ni `TimeLib`).

Elle est **multi-pays** : le fuseau (POSIX TZ) et le serveur NTP sont des paramètres de `TIME_Init()`, qui appelle lui-même `configTzTime()`. Elle fournit l'epoch UTC, l'heure/date **locale** (été/hiver automatiques), le serveur et le fuseau choisis, le statut de synchronisation et des helpers d'affichage (`String`).

Le code, les commentaires et les logs série sont en **français**.

---

## 1. Pourquoi cette librairie ?

Sur les cores ESP8266 et ESP32, le SNTP est intégré : `configTzTime()` démarre le client NTP natif **et** règle le fuseau. Réutiliser un second client (`NTPClient` + `WiFiUdp`) est donc inutile et coûteux.

| | Ancienne approche (`NTPClient`) | `Temps_Unifie` (SNTP natif) |
|---|---|---|
| Dépendances | `NTPClient` + `WiFiUdp` | **aucune** |
| Coût flash | ~2,8 Ko | 0 (déjà dans le core) |
| Socket UDP | 1 socket supplémentaire | aucune (gérée par lwIP) |
| Fuseau / été-hiver | calcul manuel | TZ POSIX passé à `TIME_Init()` |
| Multi-pays | non | oui (fuseau + serveur paramétrés) |
| Plateformes | ESP8266 + ESP32 | ESP8266 + ESP32 |

## 2. Fonctionnalités

- **Epoch UTC** (`Temps_UTC`) et décomposition date/heure via newlib.
- **Heure et date LOCALES** (`Temps_Heure_LOCAL` / `Temps_Date_LOCAL`) — fuseau et été/hiver automatiques.
- **Choix du pays** : fuseau POSIX + serveur NTP passés à `TIME_Init(fuseau, serveur)`.
- **Statut de synchronisation** (`Temps_Synchronise`, `Temps_AgeSync_s`).
- **Diagnostic SNTP** : service actif, serveur réel, mode et statut bruts.
- **Réglage de l'intervalle** de re-sync (ESP32 ; non exposé par le core ESP8266).
- **Valeurs numériques brutes** `t_UTC` / `t_LOCAL`, sans passer par les `String`.

## 3. Plateformes

- **ESP8266** (`espressif8266`) : SNTP lwIP, callback `settimeofday_cb()`. Intervalle, mode et statut SNTP non exposés par le core (voir §8).
- **ESP32** (`espressif32`) : SNTP ESP-IDF (`esp_sntp.h`), intervalle réglable, mode/statut disponibles.
- Aucun matériel supplémentaire (pas de RTC) : l'horloge système est celle du MCU.
- `configTzTime()` et `localtime_r()`/`gmtime_r()` sont disponibles sur les deux cores.

## 4. Installation

### 4.1 PlatformIO (recommandé)

Dépendance distante — **forme git complète** obligatoire (la forme courte `Fo170/Temps_Unifie` ne se résout pas sur le registre PlatformIO) :

```ini
; platformio.ini
lib_deps =
    https://github.com/Fo170/Temps_Unifie.git@^1.0.0
```

### 4.2 Arduino IDE

Télécharger le dépôt puis l'ajouter via *Croquis → Inclure une bibliothèque → Ajouter la bibliothèque .ZIP*, ou copier `src/Temps_Unifie.h` dans `~/Arduino/libraries/Temps_Unifie/src/`.

## 5. Principe d'intégration (important)

`Temps_Unifie` **ne démarre pas le Wi-Fi** : connectez-vous d'abord, puis appelez `TIME_Init(fuseau, serveur)`. Celui-ci appelle `configTzTime()` (démarrage du SNTP natif + fuseau), branche le callback de synchronisation et applique l'intervalle de 60 s.

1. **Connexion Wi-Fi** (ex. `WiFi.begin(...)`).
2. **`TIME_Init(fuseau, serveur)`**.
3. **`TIME_Maj()`** dans `loop()` — non bloquant.

```cpp
#if defined(ESP8266)
  #include <ESP8266WiFi.h>
#else
  #include <WiFi.h>
#endif
#include <Temps_Unifie.h>

#define TZ_FRANCE "CET-1CEST,M3.5.0,M10.5.0/3"

void setup() {
  WiFi.begin(__ssid__, __password__);                     // 1. Wi-Fi
  while (WiFi.status() != WL_CONNECTED) delay(100);

  TIME_Init(TZ_FRANCE, NTP_FCT_SERVEUR_NTP);              // 2. SNTP + fuseau
}

void loop() {
  TIME_Maj();                                             // 3. non bloquant
  Serial.println(Temps_Heure_LOCAL());                    // "14:05:03"
}
```

## 6. API

### Initialisation / boucle

| Fonction | Description |
|---|---|
| `void TIME_Init(const char* fuseau, const char* serveur)` | Démarre le SNTP natif (`configTzTime`), règle le fuseau POSIX, branche le callback et applique l'intervalle (60 s). À appeler après la connexion Wi-Fi. `fuseau` NULL → `"UTC0"` ; `serveur` NULL → `NTP_FCT_SERVEUR_NTP`. |
| `void TIME_Maj()` | Non bloquant, à appeler dans `loop()`. |

### Accès au temps

| Fonction | Retour |
|---|---|
| `uint32_t Temps_UTC()` | Epoch UTC en secondes |
| `bool Temps_Synchronise()` | Vrai si l'horloge est synchronisée |
| `String Temps_Serveur()` | Serveur NTP choisi (passé à `TIME_Init`) |
| `String Temps_Fuseau()` | Fuseau POSIX choisi (passé à `TIME_Init`) |
| `uint32_t Temps_DerniereSync_ms()` | `millis()` de la dernière sync (0 = jamais) |
| `uint32_t Temps_AgeSync_s()` | Âge de la dernière sync en secondes |
| `String Temps_Heure_UTC()` | `"HH:MM:SS"` UTC |
| `String Temps_Date_UTC()` | `"JJ/MM/AAAA"` UTC |
| `String Temps_Heure_LOCAL()` | `"HH:MM:SS"` heure locale |
| `String Temps_Date_LOCAL()` | `"JJ/MM/AAAA"` heure locale |

### Valeurs numériques brutes (hors String)

Deux variables globales de type `time_t`, rafraîchies par `TIME_Maj()` :

| Variable | Contenu |
|---|---|
| `t_UTC` | Epoch UTC (secondes depuis 1970) |
| `t_LOCAL` | Heure « murale » locale = `t_UTC` + décalage du fuseau |

`t_LOCAL` se décompose avec `gmtime_r()` pour lire les champs locaux (an, mois, jour, heure, minute, seconde, jour de semaine…) :

```cpp
TIME_Maj();
struct tm tl;
if (gmtime_r(&t_LOCAL, &tl)) {
  int an   = tl.tm_year + 1900;
  int mois = tl.tm_mon + 1;
  int jour = tl.tm_mday;
  int h    = tl.tm_hour, min = tl.tm_min, s = tl.tm_sec;
  int wday = tl.tm_wday, yday = tl.tm_yday;
}
```

Les helpers `Temps_Heure_UTC()`, `Temps_Date_UTC()`, `Temps_Heure_LOCAL()` et `Temps_Date_LOCAL()` rafraîchissent eux-mêmes `t_UTC`/`t_LOCAL` avant lecture : ils renvoient donc toujours l'heure exacte, même sans `TIME_Maj()` préalable.

### Diagnostic SNTP

| Fonction | Description | ESP8266 |
|---|---|---|
| `bool NTP_FCT_Actif()` | Vrai si le SNTP est actif | oui |
| `String NTP_FCT_ServeurReel()` | Serveur réellement configuré dans le SNTP | oui |
| `String NTP_FCT_ModeTexte()` | `"SMOOTH"` ou `"IMMED"` | renvoie `"POLL"` |
| `String NTP_FCT_StatutSntpTexte()` | `"COMPLETED"`, `"IN_PROGRESS"` ou `"RESET"` | déduit de `Temps_Synchronise()` |
| `uint32_t NTP_FCT_Intervalle_ms()` | Intervalle de re-sync courant (ms) | renvoie `0` |
| `uint32_t NTP_FCT_SetIntervalle_ms(uint32_t ms)` | Change et applique immédiatement l'intervalle | renvoie `0` (non supporté) |
| `bool NTP_FCT_ForcerSync()` | Force une synchronisation immédiate | renvoie `false` |

### Macros de configuration (surchargeables avant inclusion)

| Macro | Défaut | Rôle |
|---|---|---|
| `NTP_FCT_SERVEUR_NTP` | `"fr.pool.ntp.org"` | Serveur NTP par défaut (si `serveur` = NULL) |
| `NTP_FCT_FUSEAU_DEF` | `"UTC0"` | Fuseau POSIX par défaut (si `fuseau` = NULL) |
| `NTP_FCT_EPOCH_MINI` | `100000UL` | Seuil « horloge synchronisée » (sinon 1970) |
| `NTP_FCT_INTERVALLE_MIN_MS` | `60000UL` | Plancher de re-sync |
| `NTP_FCT_INTERVALLE_DEF_MS` | `60000UL` | Intervalle appliqué au boot |

## 7. Fuseaux horaires (multi-pays)

Le fuseau n'est **pas** calculé par la librairie : il vient du **TZ POSIX** passé à `TIME_Init()` (transmis à `configTzTime()`).

- `localtime_r()` → heure locale (avec été/hiver automatiques) ;
- `gmtime_r()` → UTC.

Exemples de chaînes POSIX :

| Pays / zone | Fuseau POSIX |
|---|---|
| France | `"CET-1CEST,M3.5.0,M10.5.0/3"` |
| UTC | `"UTC0"` |
| Royaume-Uni | `"GMT0BST,M3.5.0/1,M10.5.0"` |
| US Est | `"EST5EDT,M3.2.0,M11.1.0"` |
| US Pacifique | `"PST8PDT,M3.2.0,M11.1.0"` |

## 8. Points d'attention

- **ESP32** : défaut ESP-IDF = 3 h de re-sync (`CONFIG_LWIP_SNTP_UPDATE_DELAY`). `TIME_Init()` applique 60 s ; `NTP_FCT_SetIntervalle_ms()` appelle `sntp_restart()` car `sntp_set_sync_interval()` seul n'agit qu'à l'expiration du cycle courant.
- **ESP8266** : l'intervalle, le mode et le statut SNTP ne sont **pas** exposés par le core (lwIP) ; `NTP_FCT_SetIntervalle_ms`/`NTP_FCT_ForcerSync` sont sans effet, `NTP_FCT_ModeTexte` renvoie `"POLL"` et `NTP_FCT_StatutSntpTexte` est déduit de `Temps_Synchronise()`.
- `TIME_Init()` **démarre** le SNTP via `configTzTime()` : ne pas appeler `configTzTime()` en double avant.
- Sur ESP32, `sntp_get_sync_status()` est **transitoire** (repasse vite à `RESET`) : le statut fiable est `Temps_Synchronise()` + `Temps_AgeSync_s()` (via le callback).
- La librairie **ne démarre pas le Wi-Fi** : connectez-vous avant `TIME_Init()`.
- Variables d'état et fonctions sont en `static` (linkage interne) : la librairie est prévue pour être incluse dans l'unité de compilation principale (`.ino`/`main.cpp`).

## 9. Exemple

Voir `exemple/` (PlatformIO, envs `esp12e` et `esp32dev`) :

```bash
cd exemple
pio run -e esp12e            # compilation ESP8266
pio run -e esp32dev          # compilation ESP32
pio run -e esp32dev -t upload   # téléversement
pio device monitor           # moniteur série (115200 bauds)
```

## 10. Licence

GPL-3.0-only — voir [`LICENSE`](LICENSE). Auteur : **Olivier FOURNET** (org GitHub [`Fo170`](https://github.com/Fo170)).
