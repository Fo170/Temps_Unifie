// ============================================================================
//  Exemple d'utilisation de la librairie Temps_Unifie (ESP8266 + ESP32)
//
//  Montre :
//    - la connexion Wi-Fi puis TIME_Init(fuseau, serveur) ;
//    - la synchronisation SNTP native et son statut ;
//    - l'affichage UTC et LOCAL (ici la France, CET/CEST) ;
//    - le bloc HTML prêt pour une page web (Temps_Zone_WEB).
//
//  Multi-pays : changer TZ_PAYS ci-dessous (le serveur NTP est un paramètre
//  de TIME_Init, pas une constante de la librairie).
//
//  Licence : GPL-3.0-only — Auteur : Olivier FOURNET (Fo170)
// ============================================================================
#include <Arduino.h>

#if defined(ESP8266)
  #include <ESP8266WiFi.h>
#else
  #include <WiFi.h>
#endif

// Identifiants Wi-Fi : si le dossier partagé _INCLUDE_/Arduino/ est dispo
// (build_flags -I ../../..), on utilise connexions.h ; sinon on compile avec
// les valeurs de repli ci-dessous à personnaliser.
#if __has_include("connexions.h")
  #include "connexions.h"
#endif

#if defined(__ssid__)
  static const char* WIFI_SSID = __ssid__;
  static const char* WIFI_PASS = __password__;
#else
  static const char* WIFI_SSID = "votre_SSID";
  static const char* WIFI_PASS = "votre_mot_de_passe";
#endif

#include <Temps_Unifie.h>

// Fuseau France (POSIX) : heure d'hiver CET (UTC+1), été CEST (UTC+2).
// M3.5.0 = dernier dimanche de mars ; M10.5.0/3 = dernier dimanche d'octobre.
#define TZ_FRANCE   "CET-1CEST,M3.5.0,M10.5.0/3"

// Autres exemples de fuseaux (multi-pays) :
//   #define TZ_UTC   "UTC0"
//   #define TZ_US_EST "EST5EDT,M3.2.0,M11.1.0"
//   #define TZ_UK     "GMT0BST,M3.5.0/1,M10.5.0"

// Bloc HTML pour l'interface web : double affichage UTC + LOCAL, serveur, statut
static String Temps_Zone_WEB(void)
{
  String s = "<div style='margin-top:6px;font-size:13px;line-height:1.5;'>";
  s += "<b>🕒 UTC :</b> " + Temps_Date_UTC() + " " + Temps_Heure_UTC() + " UTC<br>";
  s += "<b>LOCAL  :</b> " + Temps_Date_LOCAL() + " " + Temps_Heure_LOCAL() + " <br>";
  s += "<b>Serveur NTP :</b> " + Temps_Serveur();
  if (Temps_Synchronise())
    s += " — ✅ synchronisé (il y a " + String(Temps_AgeSync_s()) + " s)";
  else
    s += " — ⏳ non synchronisé";
  s += "</div>";
  return s;
}

// ----------------------------------------------------------------------------
//  Connexion Wi-Fi (bloquante, avec délai maximal)
// ----------------------------------------------------------------------------
static bool connexionWiFi(uint32_t timeout_ms = 20000)
{
  Serial.printf("Connexion Wi-Fi a \"%s\" ", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  const uint32_t debut = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - debut) < timeout_ms)
  {
    delay(500);
    Serial.print('.');
  }
  Serial.println();

  if (WiFi.status() != WL_CONNECTED)
  {
    Serial.println("Echec de la connexion Wi-Fi.");
    return false;
  }
  Serial.print("Connecte, IP = ");
  Serial.println(WiFi.localIP());
  return true;
}

// ----------------------------------------------------------------------------
//  setup()
// ----------------------------------------------------------------------------
void setup()
{
  Serial.begin(115200);
  delay(200);
  Serial.println("\n============================================");
  Serial.println(" Temps_Unifie - exemple ESP32 (SNTP natif)");
  Serial.println("============================================");

  if (!connexionWiFi())
  {
    Serial.println("Arret : verifiez vos identifiants Wi-Fi.");
    return;
  }

  // TIME_Init(fuseau, serveur) : demarre le SNTP natif (configTzTime), regle le
  // fuseau POSIX, branche le callback de sync et l'intervalle (60 s au lieu
  // des 3 h par defaut de l'ESP-IDF).
  TIME_Init(TZ_FRANCE, NTP_FCT_SERVEUR_NTP);

  Serial.print("Fuseau : ");
  Serial.println(Temps_Fuseau());
  Serial.print("Serveur NTP configure : ");
  Serial.println(NTP_FCT_ServeurReel());
}

// ----------------------------------------------------------------------------
//  loop() — TIME_Maj() est non bloquant, l'affichage est cadencié a 5 s
// ----------------------------------------------------------------------------
void loop()
{
  TIME_Maj();

  static uint32_t dernierAffichage = 0;
  if (millis() - dernierAffichage < 5000) return;
  dernierAffichage = millis();

  Serial.println("--------------------------------------------");
  Serial.print("UTC        : ");
  Serial.print(Temps_Date_UTC());
  Serial.print(' ');
  Serial.print(Temps_Heure_UTC());
  Serial.println(" UTC");

  Serial.print("Local      : ");
  Serial.print(Temps_Date_LOCAL());
  Serial.print(' ');
  Serial.print(Temps_Heure_LOCAL());
  Serial.print(" (");
  Serial.print(Temps_Fuseau());
  Serial.println(')');

  Serial.print("Epoch UTC  : ");
  Serial.println(Temps_UTC());

  // Valeurs numeriques brutes (sans passer par les String) : t_UTC et t_LOCAL
  // sont rafraichies par TIME_Maj(). t_LOCAL se decompose avec gmtime_r().
  Serial.print("t_UTC      : ");
  Serial.println((uint32_t)t_UTC);
  Serial.print("t_LOCAL    : ");
  Serial.println((uint32_t)t_LOCAL);
  struct tm tl;
  if (gmtime_r(&t_LOCAL, &tl))
  {
    Serial.printf("Champs locaux : %04d-%02d-%02d %02d:%02d:%02d (wday=%d, yday=%d)\n",
                  tl.tm_year + 1900, tl.tm_mon + 1, tl.tm_mday,
                  tl.tm_hour, tl.tm_min, tl.tm_sec, tl.tm_wday, tl.tm_yday);
  }

  Serial.print("Synchronise: ");
  Serial.println(Temps_Synchronise() ? "oui" : "non");
  Serial.print("Age sync   : ");
  Serial.print(Temps_AgeSync_s());
  Serial.println(" s");

  // Bloc HTML pret a inserer dans une page web
  Serial.println(Temps_Zone_WEB());
}
