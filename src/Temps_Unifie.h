// ============================================================================
//  Temps_Unifie.h  —  Gestion du temps UNIFIEE pour ESP8266 + ESP32
//
//  @file      Temps_Unifie.h
//  @version   1.0.0
//  @date      2026-09-25
//  @author    Olivier FOURNET (olivier.fournet@free.fr)
//  @copyright GPL-3.0-only
//  @see       https://github.com/Fo170/Temps_Unifie
//
//  NTP « classique » MULTI-PAYS basé sur le SNTP natif : ESP-IDF (lwIP) sur
//  ESP32 et SNTP lwIP du core ESP8266. Aucune dépendance externe : ni NTPClient,
//  ni WiFiUdp, ni TimeLib. `configTzTime()` existe sur les deux plateformes.
//
//  Historique : fork de l'ancien Temps_Unifie.h (NTPClient + TimeLib). Depuis le
//  25/09/2026 : plus de client NTP externe ni de TimeLib — l'horloge système est
//  déjà synchronisée par le SNTP natif démarré par `configTzTime()`. On lit donc
//  simplement `time()` / `localtime_r()` / `gmtime_r()` (newlib).
//  → gain flash (NTPClient ~2,8 Ko) + plus de socket UDP ni de 2e client NTP.
//
//  ⚠️ Différences de plateforme (masquées par l'API commune) :
//    - ESP32   : `esp_sntp.h`, callback `sntp_set_time_sync_notification_cb()`,
//                intervalle réglable (`sntp_set_sync_interval`/`sntp_restart`),
//                mode et statut SNTP disponibles.
//    - ESP8266 : lwIP (`sntp.h` + `coredecls.h`), callback `settimeofday_cb()`,
//                intervalle/mode/statut non exposés (renvoient 0 / "POLL" /
//                dérivés de Temps_Synchronise()).
//
//  ⚠️ Multi-pays : le fuseau (POSIX TZ) et le serveur NTP sont des paramètres de
//  `TIME_Init()`, qui appelle lui-même `configTzTime()`. `localtime_r()` renvoie
//  ainsi directement l'heure LOCALE du pays choisi, sans calcul manuel d'été.
//
//  Exemples de fuseaux POSIX :
//    France  : "CET-1CEST,M3.5.0,M10.5.0/3"
//    UTC     : "UTC0"
//    US Est  : "EST5EDT,M3.2.0,M11.1.0"
//    UK      : "GMT0BST,M3.5.0/1,M10.5.0"
//
//  Usage minimal :
//    #include <Temps_Unifie.h>            // après la connexion Wi-Fi
//    TIME_Init(TZ_FRANCE, NTP_FCT_SERVEUR_NTP);   // démarre SNTP + fuseau
//    TIME_Maj();                          // loop() : refresh non bloquant
//    uint32_t utc = Temps_UTC();          // epoch UTC (s)
//    String h  = Temps_Heure_LOCAL();     // "14:05:03" (heure locale)
//    String d  = Temps_Date_LOCAL();      // "21/08/2026"
// ============================================================================
#ifndef TEMPS_UNIFIE_H
#define TEMPS_UNIFIE_H

#include <Arduino.h>
#include <time.h>

#if defined(ESP8266)
  #include <coredecls.h>   // settimeofday_cb()
  #include <sntp.h>        // sntp_enabled(), sntp_getservername() (lwIP)
#elif defined(ESP32)
  #include <esp_sntp.h>    // SNTP natif ESP-IDF
#else
  #error "Temps_Unifie ne supporte que l'ESP8266 et l'ESP32."
#endif

// ----------------------------------------------------------------------------
//  Configuration (surchargeable AVANT l'inclusion via #define)
// ----------------------------------------------------------------------------
#ifndef NTP_FCT_SERVEUR_NTP
  #define NTP_FCT_SERVEUR_NTP   "fr.pool.ntp.org"   // serveur NTP par défaut
#endif
#ifndef NTP_FCT_FUSEAU_DEF
  #define NTP_FCT_FUSEAU_DEF    "UTC0"              // fuseau POSIX par défaut
#endif
#ifndef NTP_FCT_EPOCH_MINI
  #define NTP_FCT_EPOCH_MINI    100000UL            // seuil « horloge synchronisée » (sinon 1970)
#endif
#ifndef NTP_FCT_INTERVALLE_MIN_MS
  #define NTP_FCT_INTERVALLE_MIN_MS  60000UL        // plancher de re-sync (le défaut IDF est 3 h !)
#endif
#ifndef NTP_FCT_INTERVALLE_DEF_MS
  #define NTP_FCT_INTERVALLE_DEF_MS  60000UL        // intervalle appliqué au boot
#endif

// ----------------------------------------------------------------------------
//  État (la sync est entretenue par le SNTP natif, pas ici)
// ----------------------------------------------------------------------------
static volatile uint32_t ntpFctDerniereSync_ms = 0;   // millis() de la dernière sync SNTP
static volatile bool     ntpFctSynchronise     = false;
static const char*       ntpFctServeur         = NTP_FCT_SERVEUR_NTP;  // serveur NTP choisi
static const char*       ntpFctFuseau          = NTP_FCT_FUSEAU_DEF;   // fuseau POSIX choisi

// Valeurs numériques brutes (rafraîchies par tempsMajVariables(), appelée dans
// TIME_Init(), TIME_Maj() et chaque fonction d'accès) — pour accéder aux champs
// date/heure sans passer par les String :
//   t_UTC   : epoch UTC (secondes depuis 1970)
//   t_LOCAL : epoch « mural » local = t_UTC + décalage du fuseau ; à décomposer
//             avec gmtime_r(&t_LOCAL, &tm) pour lire an/mois/jour/h/m/s locaux.
static time_t t_UTC   = 0;
static time_t t_LOCAL = 0;

// Callback appelé par le SNTP natif à chaque synchronisation réussie.
//  - ESP32   : fourni par lwIP, reçoit l'instant de synchro (ignoré).
//  - ESP8266 : `settimeofday_cb()` ne transmet aucun paramètre.
#if defined(ESP8266)
static void ntpFctOnSync(void)
#else
static void ntpFctOnSync(struct timeval* tv)
#endif
{
#if defined(ESP32)
  (void)tv;
#endif
  ntpFctDerniereSync_ms = millis();
  ntpFctSynchronise     = true;
}

// ----------------------------------------------------------------------------
//  Réglage du rafraîchissement SNTP natif
//  ⚠️ Le défaut IDF (CONFIG_LWIP_SNTP_UPDATE_DELAY) est de 3 h ; on applique
//     NTP_FCT_INTERVALLE_DEF_MS au boot (voir TIME_Init).
// ----------------------------------------------------------------------------

// Intervalle de re-sync courant, en millisecondes.
// Renvoie 0 sur ESP8266 (intervalle lwIP non exposé par le core).
static uint32_t NTP_FCT_Intervalle_ms(void)   // intervalle courant (ms)
{
#if defined(ESP32)
  return sntp_get_sync_interval();
#else
  return 0;
#endif
}

// Change l'intervalle de re-sync SNTP (ms) et l'applique IMMÉDIATEMENT.
// ⚠️ sntp_set_sync_interval() seul n'agit qu'à l'expiration du cycle en cours
//    (jusqu'à 3 h) → on appelle sntp_restart() pour re-armer tout de suite.
// ESP8266 : non supporté (renvoie 0).
// @param ms  intervalle voulu (borné à NTP_FCT_INTERVALLE_MIN_MS)
// @return la valeur réellement appliquée
static uint32_t NTP_FCT_SetIntervalle_ms(uint32_t ms)
{
#if defined(ESP32)
  if (ms < NTP_FCT_INTERVALLE_MIN_MS) ms = NTP_FCT_INTERVALLE_MIN_MS;
  sntp_set_sync_interval(ms);
  sntp_restart();
  return sntp_get_sync_interval();
#else
  (void)ms;
  return 0;
#endif
}

// Force une synchronisation immédiate (garde l'intervalle courant).
// ESP8266 : non exposé, renvoie false.
// @return true si le redémarrage du SNTP a été accepté
static bool NTP_FCT_ForcerSync(void)
{
#if defined(ESP32)
  return sntp_restart();
#else
  return false;
#endif
}

// ----------------------------------------------------------------------------
//  Init / Maj
// ----------------------------------------------------------------------------

// Rafraîchit les variables numériques t_UTC / t_LOCAL depuis l'horloge système.
// Appelée par TIME_Maj(), TIME_Init() ET chaque fonction d'accès : toute lecture
// renvoie donc un temps exact, et t_LOCAL reste cohérent avec t_UTC.
// t_LOCAL est l'heure « murale » locale : t_UTC + décalage du fuseau, à
// décomposer avec gmtime_r(&t_LOCAL, &tm) pour lire an/mois/jour/h/m/s locaux.
// Le décalage est calculé via mktime() (timegm() n'est pas exposé par newlib).
static void tempsMajVariables(void)
{
  t_UTC = time(nullptr);

  struct tm g;
  if (!gmtime_r(&t_UTC, &g)) { t_LOCAL = t_UTC; return; }

  g.tm_isdst = -1;                         // laisse mktime résoudre l'heure d'été
  time_t as_local = mktime(&g);            // epoch des champs UTC interprétés en local
  if (as_local == (time_t)-1) { t_LOCAL = t_UTC; return; }

  t_LOCAL = t_UTC + (t_UTC - as_local);    // t_UTC + décalage du fuseau
}

// Initialise la couche temps : démarre le SNTP natif, règle le fuseau puis
// branche le callback de synchronisation et l'intervalle de re-sync.
// ⚠️ À appeler APRÈS la connexion Wi-Fi (le SNTP doit pouvoir joindre le serveur).
// @param fuseau  fuseau POSIX (ex. "CET-1CEST,M3.5.0,M10.5.0/3") ; si NULL → "UTC0"
// @param serveur serveur NTP (ex. "fr.pool.ntp.org") ; si NULL → NTP_FCT_SERVEUR_NTP
static void TIME_Init(const char* fuseau, const char* serveur)
{
  if (!fuseau)  fuseau  = NTP_FCT_FUSEAU_DEF;
  if (!serveur) serveur = NTP_FCT_SERVEUR_NTP;

  ntpFctFuseau  = fuseau;
  ntpFctServeur = serveur;

  // Branche le callback AVANT le démarrage pour capter la 1re synchronisation.
#if defined(ESP8266)
  settimeofday_cb(ntpFctOnSync);
#else
  sntp_set_time_sync_notification_cb(ntpFctOnSync);
#endif

  // Démarre le client SNTP natif (ESP-IDF ou lwIP) ET applique le fuseau POSIX.
  configTzTime(ntpFctFuseau, ntpFctServeur);

#if defined(ESP32)
  // Ramène l'intervalle au défaut voulu (60 s) au lieu des 3 h de l'IDF.
  NTP_FCT_SetIntervalle_ms(NTP_FCT_INTERVALLE_DEF_MS);
#endif

  // Rafraîchit t_UTC / t_LOCAL ; si l'horloge est déjà réglée (ex. init
  // multiple), cale aussi l'âge de la dernière synchro.
  tempsMajVariables();
  if (t_UTC >= (time_t)NTP_FCT_EPOCH_MINI && ntpFctDerniereSync_ms == 0)
    ntpFctDerniereSync_ms = millis();
}

// À appeler régulièrement dans loop(). Non bloquant : rafraîchit t_UTC / t_LOCAL
// et marque l'horloge comme synchronisée dès que l'epoch a dépassé le seuil.
static void TIME_Maj(void)
{
  tempsMajVariables();
  if (t_UTC >= (time_t)NTP_FCT_EPOCH_MINI) ntpFctSynchronise = true;
}

// ----------------------------------------------------------------------------
//  Accès
//  ⚠️ Chaque fonction rafraîchit t_UTC / t_LOCAL avant lecture : le temps
//     renvoyé est donc toujours exact, même sans appel préalable à TIME_Maj().
// ----------------------------------------------------------------------------

// Epoch UTC courant (secondes depuis 1970). 0 si l'horloge n'est pas réglée.
static uint32_t Temps_UTC(void)
{
  tempsMajVariables();
  return (uint32_t)t_UTC;
}

// Horloge considérée comme synchronisée (callback reçu OU epoch plausible).
static bool Temps_Synchronise(void)
{
  tempsMajVariables();
  return ntpFctSynchronise || (t_UTC >= (time_t)NTP_FCT_EPOCH_MINI);
}

// Serveur NTP choisi (passé à TIME_Init).
static String   Temps_Serveur(void)      { return ntpFctServeur; }

// Fuseau POSIX choisi (passé à TIME_Init).
static String   Temps_Fuseau(void)       { return ntpFctFuseau; }

// millis() de la dernière synchronisation (0 = jamais).
static uint32_t Temps_DerniereSync_ms(void) { return ntpFctDerniereSync_ms; }

// Âge de la dernière synchronisation, en secondes (0 si jamais synchronisé).
static uint32_t Temps_AgeSync_s(void)
{
  if (!ntpFctDerniereSync_ms) return 0;
  return (millis() - ntpFctDerniereSync_ms) / 1000;
}

// ----------------------------------------------------------------------------
//  Statut du SNTP natif (informations de diagnostic)
//  ⚠️ sntp_get_sync_status() est TRANSITOIRE (repasse vite à RESET) : le
//     statut fiable reste Temps_Synchronise() + Temps_AgeSync_s() (callback).
// ----------------------------------------------------------------------------

// Vrai si le service SNTP est actif.
static bool NTP_FCT_Actif(void)            { return sntp_enabled(); }

// Serveur NTP réellement configuré dans le SNTP (fallback serveur choisi).
static String NTP_FCT_ServeurReel(void)     // serveur réellement configuré dans le SNTP
{
#if defined(ESP8266)
  const char* s = sntp_getservername(0);
#else
  const char* s = esp_sntp_getservername(0);
#endif
  return s ? String(s) : String(ntpFctServeur);
}

// Mode de synchronisation lisible : "SMOOTH"/"IMMED" (ESP32) ou "POLL" (ESP8266).
static String NTP_FCT_ModeTexte(void)
{
#if defined(ESP32)
  return (sntp_get_sync_mode() == SNTP_SYNC_MODE_SMOOTH) ? "SMOOTH" : "IMMED";
#else
  return "POLL";   // mode unique du SNTP lwIP sur ESP8266
#endif
}

// Statut SNTP brut lisible : "COMPLETED", "IN_PROGRESS" ou "RESET".
static String NTP_FCT_StatutSntpTexte(void)
{
#if defined(ESP32)
  switch (sntp_get_sync_status())
  {
    case SNTP_SYNC_STATUS_COMPLETED:   return "COMPLETED";
    case SNTP_SYNC_STATUS_IN_PROGRESS: return "IN_PROGRESS";
    default:                           return "RESET";
  }
#else
  // lwIP ESP8266 n'expose pas le statut : on le déduit de l'heure système.
  return Temps_Synchronise() ? "COMPLETED" : "RESET";
#endif
}

// ----------------------------------------------------------------------------
//  Helpers d'affichage
//  UTC via gmtime_r ; heure LOCALE via localtime_r (fuseau POSIX de configTzTime)
//  Chaque helper rafraîchit d'abord t_UTC / t_LOCAL : le temps affiché est exact.
// ----------------------------------------------------------------------------

// Heure UTC au format "HH:MM:SS".
static String Temps_Heure_UTC(void)
{
  tempsMajVariables();
  struct tm g;
  if (!gmtime_r(&t_UTC, &g)) return "--:--:--";
  char buf[10];
  snprintf(buf, sizeof(buf), "%02d:%02d:%02d", g.tm_hour, g.tm_min, g.tm_sec);
  return String(buf);
}

// Date UTC au format "JJ/MM/AAAA".
static String Temps_Date_UTC(void)
{
  tempsMajVariables();
  struct tm g;
  if (!gmtime_r(&t_UTC, &g)) return "--/--/----";
  char buf[12];
  snprintf(buf, sizeof(buf), "%02d/%02d/%04d", g.tm_mday, g.tm_mon + 1, g.tm_year + 1900);
  return String(buf);
}

// Heure LOCALE au format "HH:MM:SS" (fuseau POSIX passé à TIME_Init).
static String Temps_Heure_LOCAL(void)
{
  tempsMajVariables();
  struct tm l;
  if (!localtime_r(&t_UTC, &l)) return "--:--:--";
  char buf[10];
  snprintf(buf, sizeof(buf), "%02d:%02d:%02d", l.tm_hour, l.tm_min, l.tm_sec);
  return String(buf);
}

// Date LOCALE au format "JJ/MM/AAAA" (fuseau POSIX passé à TIME_Init).
static String Temps_Date_LOCAL(void)
{
  tempsMajVariables();
  struct tm l;
  if (!localtime_r(&t_UTC, &l)) return "--/--/----";
  char buf[12];
  snprintf(buf, sizeof(buf), "%02d/%02d/%04d", l.tm_mday, l.tm_mon + 1, l.tm_year + 1900);
  return String(buf);
}

#endif // TEMPS_UNIFIE_H
