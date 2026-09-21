#include "i18n.h"
#include "shared/os.h"

#include <stdio.h>
#include <string.h>
#include <wchar.h>

static const wchar_t *k_en[I18N_COUNT] = {
    L"Settings",
    L"User",
    L"General",
    L"Installation",
    L"App language",
    L"Language of the launcher itself.",
    L"Game language",
    L"Used when Dawn downloads the language depot.",
    L"English",
    L"Deutsch",
    L"Browse",
    L"Copy ID",
    L"Copied",
    L"Not set",
    L"OK",
    L"Owned",
    L"Missing",
    L"Unknown",
    L"Guest",
    L"Signed in",
    L"Not signed in",
    L"Signed in with Steam",
    L"Sign in to check DLC",
    L"Checking licenses",
    L"All required DLC owned",
    L"Required DLC missing",
    L"Can't verify DLC",
    L"DLC ownership",
    L"Install folder",
    L"Game EXE",
    L"Folder is locked while downloading.",
    L"Browse to a folder first.",
    L"OK starts the download here.",
    L"Dawn is installed in this folder.",
    L"Downloads go in the folder. Play uses the EXE.",
    L"New Version",
    L"Sign out",
    L"Sign in",
    L"Update",
    L"Installing",
    L"Starting",
    L"Not now",
    L"Downloading the new launcher and replacing this install.",
    L"Download the official build, replace this launcher, and restart.",
    L"Continue with Steam",
    L"Sign in with Steam",
    L"Steam Guard",
    L"Steam username",
    L"Steam password",
    L"Code from the Steam app",
    L"DepotDownloader account name",
    L"Steam password for this download",
    L"Type or paste the Guard code, then continue.",
    L"The username you use to sign in to Steam.",
    L"Deleted after the download.",
    L"Guard code",
    L"Username",
    L"Password",
    L"Continue",
    L"Play",
    L"Install",
    L"Download",
    L"Stop",
    L"Cancel",
    L"Resume",
    L"Pause",
    L"Working",
    L"Verify files",
    L"Simulate download",
    L"Confirm uninstall",
    L"Uninstall Dawn",
    L"Uninstall Sunrise",
    L"Uninstall all",
    L"Stop Destiny 2",
    L"Cancel download",
    L"Uninstall",
    L"PLAY",
    L"PAUSE",
    L"English",
    L"French",
    L"German",
    L"Italian",
    L"Japanese",
    L"Portuguese (Brazil)",
    L"Spanish",
    L"Russian",
    L"Polish",
    L"Chinese (Simplified)",
    L"Chinese (Traditional)",
    L"Spanish (Latam)",
    L"Korean"
};

static const wchar_t *k_de[I18N_COUNT] = {
    L"Einstellungen",
    L"Benutzer",
    L"Allgemein",
    L"Installation",
    L"App-Sprache",
    L"Sprache der Launcher-Oberfl\u00e4che.",
    L"Spielsprache",
    L"Wird verwendet, wenn Dawn das Sprachdepot l\u00e4dt.",
    L"English",
    L"Deutsch",
    L"Durchsuchen",
    L"ID kopieren",
    L"Kopiert",
    L"Nicht festgelegt",
    L"OK",
    L"Vorhanden",
    L"Fehlt",
    L"Unbekannt",
    L"Gast",
    L"Angemeldet",
    L"Nicht angemeldet",
    L"Mit Steam angemeldet",
    L"Zum Pr\u00fcfen der DLCs anmelden",
    L"Lizenzen werden gepr\u00fcft",
    L"Alle ben\u00f6tigten DLCs vorhanden",
    L"Ben\u00f6tigte DLCs fehlen",
    L"DLCs k\u00f6nnen nicht gepr\u00fcft werden",
    L"DLC-Besitz",
    L"Installationsordner",
    L"Spiel-EXE",
    L"Ordner ist w\u00e4hrend des Downloads gesperrt.",
    L"Zuerst einen Ordner w\u00e4hlen.",
    L"OK startet den Download hier.",
    L"Dawn ist in diesem Ordner installiert.",
    L"Downloads landen im Ordner. Play nutzt die EXE.",
    L"Neue Version",
    L"Abmelden",
    L"Anmelden",
    L"Aktualisieren",
    L"Wird installiert",
    L"Wird gestartet",
    L"Nicht jetzt",
    L"L\u00e4dt den neuen Launcher und ersetzt diese Installation.",
    L"Offiziellen Build laden, diesen Launcher ersetzen und neu starten.",
    L"Mit Steam fortfahren",
    L"Mit Steam anmelden",
    L"Steam Guard",
    L"Steam-Benutzername",
    L"Steam-Passwort",
    L"Code aus der Steam-App",
    L"DepotDownloader-Kontoname",
    L"Steam-Passwort f\u00fcr diesen Download",
    L"Guard-Code eingeben oder einf\u00fcgen, dann weiter.",
    L"Der Benutzername f\u00fcr die Steam-Anmeldung.",
    L"Wird nach dem Download gel\u00f6scht.",
    L"Guard-Code",
    L"Benutzername",
    L"Passwort",
    L"Weiter",
    L"Spielen",
    L"Installieren",
    L"Herunterladen",
    L"Beenden",
    L"Abbrechen",
    L"Fortsetzen",
    L"Pause",
    L"Arbeitet",
    L"Dateien pr\u00fcfen",
    L"Download simulieren",
    L"Deinstallation best\u00e4tigen",
    L"Dawn deinstallieren",
    L"Sunrise deinstallieren",
    L"Alles deinstallieren",
    L"Destiny 2 beenden",
    L"Download abbrechen",
    L"Deinstallieren",
    L"PLAY",
    L"PAUSE",
    L"Englisch",
    L"Franz\u00f6sisch",
    L"Deutsch",
    L"Italienisch",
    L"Japanisch",
    L"Portugiesisch (Brasilien)",
    L"Spanisch",
    L"Russisch",
    L"Polnisch",
    L"Chinesisch (vereinfacht)",
    L"Chinesisch (traditionell)",
    L"Spanisch (Lateinamerika)",
    L"Koreanisch"
};

typedef struct StatusMap {
    const char *en;
    const wchar_t *de;
} StatusMap;

static const StatusMap k_status[] = {
    { "Starting", L"Wird gestartet" },
    { "Running", L"L\u00e4uft" },
    { "Dawn is ready", L"Dawn ist bereit" },
    { "Using cached game files", L"Nutze zwischengespeicherte Spieldateien" },
    { "Signing in", L"Anmeldung l\u00e4uft" },
    { "Checking files", L"Dateien werden gepr\u00fcft" },
    { "Downloading language", L"Sprache wird heruntergeladen" },
    { "Downloading game", L"Spiel wird heruntergeladen" },
    { "Enter password", L"Passwort eingeben" },
    { "Confirm Guard on phone", L"Guard am Telefon best\u00e4tigen" },
    { "Enter Guard code", L"Guard-Code eingeben" },
    { "Opening Steam", L"Steam wird ge\u00f6ffnet" },
    { "Checking", L"Pr\u00fcfung" },
    { "Downloading", L"Download" },
    { "Finishing check", L"Pr\u00fcfung wird abgeschlossen" },
    { "Finishing", L"Wird abgeschlossen" },
    { "Getting scripts", L"Skripte werden geholt" },
    { "Updating scripts", L"Skripte werden aktualisiert" },
    { "Installing scripts", L"Skripte werden installiert" },
    { "Reconnecting", L"Verbindung wird neu aufgebaut" },
    { "Files verified", L"Dateien gepr\u00fcft" },
    { "Need Dawn overlay", L"Dawn-Overlay fehlt" },
    { "Install incomplete", L"Installation unvollst\u00e4ndig" },
    { "Connecting Steam", L"Verbindung zu Steam" },
    { "Enter Steam username", L"Steam-Benutzernamen eingeben" },
    { "Need Steam login for language", L"Steam-Login f\u00fcr Sprache n\u00f6tig" },
    { "Repairing game files", L"Spieldateien werden repariert" },
    { "Downloading Dawn", L"Dawn wird heruntergeladen" },
    { "Dawn failed to start", L"Dawn konnte nicht starten" },
    { "Ready to install Dawn", L"Bereit, Dawn zu installieren" },
    { "Installing Dawn", L"Dawn wird installiert" },
    { "Finding Dawn", L"Dawn wird gesucht" },
    { "Extracting Dawn", L"Dawn wird entpackt" },
    { "Cancelled", L"Abgebrochen" },
    { "Ready to download", L"Bereit zum Download" },
    { "Steam login failed", L"Steam-Login fehlgeschlagen" },
    { "Ready", L"Bereit" },
    { "Folder locked", L"Ordner gesperrt" },
    { "Live D2 folder, not 86657", L"Live-D2-Ordner, nicht 86657" },
    { "Folder set", L"Ordner gesetzt" },
    { "EXE path saved", L"EXE-Pfad gespeichert" },
    { "Game EXE set", L"Spiel-EXE gesetzt" },
    { "Already installed", L"Bereits installiert" },
    { "No Destiny 2 license", L"Keine Destiny-2-Lizenz" },
    { "No Steam username", L"Kein Steam-Benutzername" },
    { "No install folder", L"Kein Installationsordner" },
    { "DepotDownloader missing", L"DepotDownloader fehlt" },
    { "Paused", L"Pausiert" },
    { "Need language depot", L"Sprachdepot fehlt" },
    { "Game files incomplete", L"Spieldateien unvollst\u00e4ndig" },
    { "Dawn not installed", L"Dawn nicht installiert" },
    { "Stopped", L"Beendet" },
    { "Busy, can't uninstall", L"Besch\u00e4ftigt, Deinstallation nicht m\u00f6glich" },
    { "Close Destiny 2 first", L"Zuerst Destiny 2 schlie\u00dfen" },
    { "Nothing to uninstall", L"Nichts zum Deinstallieren" },
    { "Folder looks unsafe", L"Ordner sieht unsicher aus" },
    { "Won't delete Steam install", L"Steam-Installation wird nicht gel\u00f6scht" },
    { "Could not remove Dawn", L"Dawn konnte nicht entfernt werden" },
    { "Could not remove Sunrise", L"Sunrise konnte nicht entfernt werden" },
    { "Steam is ready to download", L"Steam ist bereit zum Download" },
    { "Login saved", L"Login gespeichert" },
    { "Sign in first", L"Zuerst anmelden" },
    { "Can't verify Forsaken and Shadowkeep", L"Forsaken und Shadowkeep k\u00f6nnen nicht gepr\u00fcft werden" },
    { "Need Forsaken and Shadowkeep", L"Forsaken und Shadowkeep erforderlich" },
    { "Need Forsaken", L"Forsaken erforderlich" },
    { "Need Shadowkeep", L"Shadowkeep erforderlich" },
    { "Signed in", L"Angemeldet" },
    { "Login data missing", L"Login-Daten fehlen" },
    { "Steam confirm failed", L"Steam-Best\u00e4tigung fehlgeschlagen" },
    { "Sign in cancelled", L"Anmeldung abgebrochen" },
    { "Steam login missing", L"Steam-Login fehlt" },
    { "Sign-in cancelled", L"Anmeldung abgebrochen" },
    { "Steam ID missing", L"Steam-ID fehlt" },
    { "Confirming login", L"Login wird best\u00e4tigt" },
    { "Login invalid", L"Login ung\u00fcltig" },
    { "Loading profile", L"Profil wird geladen" },
    { "Profile failed", L"Profil fehlgeschlagen" },
    { "Loading avatar", L"Avatar wird geladen" },
    { "Checking licenses", L"Lizenzen werden gepr\u00fcft" },
    { "Login failed to start", L"Login konnte nicht starten" },
    { "Finish sign-in in browser", L"Anmeldung im Browser abschlie\u00dfen" },
    { "Signed out", L"Abgemeldet" },
    { "Update folder missing", L"Update-Ordner fehlt" },
    { "Update package incomplete", L"Update-Paket unvollst\u00e4ndig" },
    { "Could not write updater", L"Updater konnte nicht geschrieben werden" },
    { "Could not start updater", L"Updater konnte nicht gestartet werden" },
    { "Restarting", L"Neustart" },
    { "Starting download", L"Download wird gestartet" },
    { "Update download failed", L"Update-Download fehlgeschlagen" },
    { "Update download was not a package", L"Update-Download war kein Paket" },
    { "Installing update", L"Update wird installiert" },
    { "Update extract failed", L"Update-Entpacken fehlgeschlagen" },
    { "Checking for update", L"Suche nach Update" },
    { "Could not start download", L"Download konnte nicht starten" },
    { "Update cancelled", L"Update abgebrochen" },
    { "Working", L"Arbeitet" }
};

#define STATUS_COUNT ((int)(sizeof(k_status) / sizeof(k_status[0])))

static I18nLang g_lang = I18N_EN;
static int g_ready;
static char g_utf8[I18N_COUNT][96];
static int g_utf8_lang = -1;
static wchar_t g_status_w[160];
static char g_status_utf8[192];

static int
wide_to_utf8(const wchar_t *w, char *out, int max)
{
    int n = 0;

    if (!out || max < 1) {
        return 0;
    }
    out[0] = 0;
    if (!w) {
        return 0;
    }
    while (*w && n + 4 < max) {
        unsigned int cp = (unsigned int)*w++;
        if (cp < 0x80u) {
            out[n++] = (char)cp;
        } else if (cp < 0x800u) {
            out[n++] = (char)(0xc0u | (cp >> 6));
            out[n++] = (char)(0x80u | (cp & 0x3fu));
        } else {
            out[n++] = (char)(0xe0u | (cp >> 12));
            out[n++] = (char)(0x80u | ((cp >> 6) & 0x3fu));
            out[n++] = (char)(0x80u | (cp & 0x3fu));
        }
    }
    out[n] = 0;
    return n;
}

static const wchar_t *const *
table_for(I18nLang lang)
{
    return lang == I18N_DE ? k_de : k_en;
}

static void
fill_utf8(void)
{
    const wchar_t *const *table = table_for(g_lang);
    int i;

    if (g_utf8_lang == (int)g_lang) {
        return;
    }
    for (i = 0; i < I18N_COUNT; i++) {
        wide_to_utf8(table[i], g_utf8[i], (int)sizeof(g_utf8[i]));
    }
    g_utf8_lang = (int)g_lang;
}

static I18nLang
parse_lang(const char *text)
{
    if (text && (os_stricmp(text, "de") == 0 || os_stricmp(text, "german") == 0)) {
        return I18N_DE;
    }
    return I18N_EN;
}

static void
persist(void)
{
    char dawn[MAX_PATH];
    char path[MAX_PATH];
    FILE *file;

    os_data_dir(dawn, sizeof(dawn));
    if (!os_join(path, sizeof(path), dawn, "app_lang.txt")) {
        return;
    }
    file = fopen(path, "wb");
    if (!file) {
        return;
    }
    fputs(g_lang == I18N_DE ? "de" : "en", file);
    fclose(file);
}

static void
load(void)
{
    char dawn[MAX_PATH];
    char path[MAX_PATH];
    char line[32];
    FILE *file;
    size_t n;

    os_data_dir(dawn, sizeof(dawn));
    if (!os_join(path, sizeof(path), dawn, "app_lang.txt")) {
        return;
    }
    file = fopen(path, "rb");
    if (!file) {
        return;
    }
    if (fgets(line, (int)sizeof(line), file)) {
        n = strlen(line);
        while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) {
            line[--n] = '\0';
        }
        if (line[0]) {
            g_lang = parse_lang(line);
        }
    }
    fclose(file);
}

static const wchar_t *
lookup_status(const char *en)
{
    int i;

    if (!en || !en[0]) {
        return NULL;
    }
    for (i = 0; i < STATUS_COUNT; i++) {
        if (strcmp(k_status[i].en, en) == 0) {
            return k_status[i].de;
        }
    }
    return NULL;
}

void
i18n_init(void)
{
    if (g_ready) {
        return;
    }
    g_lang = I18N_EN;
    load();
    g_utf8_lang = -1;
    g_ready = 1;
}

I18nLang
i18n_lang(void)
{
    i18n_init();
    return g_lang;
}

void
i18n_set(I18nLang lang)
{
    i18n_init();
    if (lang != I18N_DE) {
        lang = I18N_EN;
    }
    if (g_lang == lang) {
        return;
    }
    g_lang = lang;
    g_utf8_lang = -1;
    persist();
}

const wchar_t *
i18n_t(I18nId id)
{
    i18n_init();
    if (id < 0 || id >= I18N_COUNT) {
        return L"";
    }
    return table_for(g_lang)[id];
}

const char *
i18n_tu(I18nId id)
{
    i18n_init();
    if (id < 0 || id >= I18N_COUNT) {
        return "";
    }
    fill_utf8();
    return g_utf8[id];
}

const wchar_t *
i18n_status_w(const char *english)
{
    const wchar_t *de;

    i18n_init();
    if (!english || !english[0]) {
        return L"";
    }
    if (g_lang == I18N_EN) {
        os_utf8_to_wide(english, g_status_w, 160);
        return g_status_w;
    }
    de = lookup_status(english);
    if (de) {
        return de;
    }
    os_utf8_to_wide(english, g_status_w, 160);
    return g_status_w;
}

const char *
i18n_status(const char *english)
{
    const wchar_t *de;

    i18n_init();
    if (!english) {
        return "";
    }
    if (g_lang == I18N_EN || !english[0]) {
        return english;
    }
    de = lookup_status(english);
    if (!de) {
        return english;
    }
    wide_to_utf8(de, g_status_utf8, (int)sizeof(g_status_utf8));
    return g_status_utf8;
}
