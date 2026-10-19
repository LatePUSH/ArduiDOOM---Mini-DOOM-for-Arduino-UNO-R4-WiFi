/*
  MINI DOOM v2 - Arduino UNO R4 WiFi + écran TFT SPI 4" 480x320 + manette iPhone (BLE)
  Moteur 3D en raycasting + monstres 

  Branchement écran :
    VCC -> 5V      GND -> GND
    CS  -> D10     RESET -> D8     DC -> D9
    SDI -> D11     SCK -> D13      LED -> 3.3V

  Bibliothèques à installer :
    - ArduinoBLE
    - GFX Library for Arduino (de Moon On Our Nation)
*/

#include <ArduinoBLE.h>
#include <Arduino_GFX_Library.h>

// ====== Réglages écran ======
#define ECRAN_ILI9488 0      // 0 = puce ST7796S, 1 = puce ILI9488
#define ROTATION      1      // 1 = paysage, 3 = paysage retourné
#define COULEURS_IPS  false  // true si les couleurs sont inversées
// ============================

#define TFT_CS  10
#define TFT_DC  9
#define TFT_RST 8

Arduino_DataBus *bus = new Arduino_HWSPI(TFT_DC, TFT_CS);
#if ECRAN_ILI9488
Arduino_GFX *gfx = new Arduino_ILI9488_18bit(bus, TFT_RST, ROTATION, COULEURS_IPS);
#else
Arduino_GFX *gfx = new Arduino_ST7796(bus, TFT_RST, ROTATION, COULEURS_IPS);
#endif

// ====== Réglages du jeu ======
const int   LARGEUR         = 480;
const int   VUE_H           = 240;              // hauteur de la vue 3D
const int   COL_L           = 4;                // largeur d'une colonne (6 ou 8 = plus rapide)
const int   NB_RAYONS       = LARGEUR / COL_L;
const float FACTEUR_HAUTEUR = 1.3f;             // hauteur apparente des murs
const float PLAN            = 0.8f;             // champ de vision
const float VITESSE         = 3.0f;             // cases par seconde
const float VITESSE_ROT     = 2.5f;             // radians par seconde
const float MARGE           = 0.2f;             // distance minimale aux murs
const int   ZONE_MORTE      = 10;
const unsigned long CADENCE_TIR = 300;          // ms entre deux tirs

// Monstres
const int   NB_ENNEMIS       = 6;
const int   VIE_ENNEMI       = 2;               // tirs pour en tuer un
const float VITESSE_ENNEMI   = 1.3f;            // cases par seconde
const float DISTANCE_VUE     = 8.0f;            // distance à laquelle ils te repèrent
const float PORTEE_ATTAQUE   = 1.0f;
const int   DEGATS           = 10;
const unsigned long CADENCE_ATTAQUE = 1000;     // ms entre deux attaques
const float TAILLE_MONSTRE   = 0.7f;

struct Ennemi {
  float x, y;
  int8_t vie;
  bool vivant;
  bool alerte;
  unsigned long dernierCoup;
  unsigned long flashJusqua;
};

struct Sprite {
  int idx;
  float prof;
  int centreX, gauche, largeur, haut, hauteur;
};

Ennemi ennemis[NB_ENNEMIS];
Sprite sprites[NB_ENNEMIS];
int nbSprites = 0;

// ====== Couleurs (RGB565) ======
const uint16_t NOIR       = 0x0000;
const uint16_t BLANC      = 0xFFFF;
const uint16_t PLAFOND    = 0x2104;
const uint16_t SOL        = 0x3186;
const uint16_t HUD_FOND   = 0x18E3;
const uint16_t HUD_BORD   = 0x4A69;
const uint16_t ROUGE      = 0xF800;
const uint16_t JAUNE      = 0xFFE0;
const uint16_t VERT       = 0x07E0;
const uint16_t GRIS       = 0x8410;
const uint16_t GRIS_CLAIR = 0xC618;
const uint16_t GRIS_FONCE = 0x4208;
const uint16_t CARTE_VIDE = 0x0841;

const uint16_t COULEURS_MURS[] = { 0x0000, 0xB0A2, 0x3D27, 0x329A, 0xC5A8 };

// ====== Le monstre (16 x 16) ======
// ' ' = transparent, R = rouge, D = rouge foncé, Y = jaune, K = noir, W = blanc
const char* MONSTRE[16] = {
  "    R      R    ",
  "   RR      RR   ",
  "   RRDDDDDDRR   ",
  "  RDDDDDDDDDDR  ",
  "  RDYYDDDDYYDR  ",
  "  RDYKDDDDKYDR  ",
  "  RDDDDDDDDDDR  ",
  "  RDKWWWWWWKDR  ",
  "  RDDKWKWKWDDR  ",
  "   RDDDDDDDDR   ",
  " RRRDDDDDDDDRRR ",
  "RDD RDDDDDDR DDR",
  "RD  RDDDDDDR  DR",
  "    RDD  DDR    ",
  "    RD    DR    ",
  "   RRR    RRR   ",
};

uint16_t couleurTexel(char t) {
  switch (t) {
    case 'R': return 0xF800;
    case 'D': return 0x9000;
    case 'Y': return 0xFFE0;
    case 'W': return 0xFFFF;
    default:  return 0x0000;
  }
}

// ====== Carte ======
const int CARTE_T = 16;
const uint8_t CARTE[CARTE_T][CARTE_T] = {
  {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
  {1,0,0,0,0,0,1,0,0,0,0,0,0,0,0,1},
  {1,0,0,0,0,0,1,0,0,0,0,0,0,0,0,1},
  {1,0,0,2,2,0,1,0,0,3,3,3,0,0,0,1},
  {1,0,0,2,2,0,0,0,0,3,0,0,0,0,0,1},
  {1,0,0,0,0,0,1,0,0,3,0,0,0,4,0,1},
  {1,1,1,0,1,1,1,0,0,0,0,0,0,4,0,1},
  {1,0,0,0,0,0,0,0,0,0,0,0,0,4,0,1},
  {1,0,0,0,0,0,0,0,0,1,1,0,1,1,0,1},
  {1,0,4,4,0,0,0,0,0,1,0,0,0,1,0,1},
  {1,0,4,0,0,0,2,0,0,1,0,0,0,1,0,1},
  {1,0,0,0,0,0,2,0,0,1,0,0,0,0,0,1},
  {1,0,0,0,0,0,2,0,0,1,1,1,0,1,0,1},
  {1,0,3,3,0,0,0,0,0,0,0,0,0,1,0,1},
  {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
  {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
};

// Position de départ des monstres
const float DEPARTS_ENNEMIS[NB_ENNEMIS][2] = {
  {10.5f, 1.5f}, {14.5f, 2.5f}, {11.5f, 4.5f},
  {3.5f, 10.5f}, {11.5f, 10.5f}, {7.5f, 13.5f}
};

const int MINI_X = 404;
const int MINI_Y = 248;
const int MINI_CASE = 4;

// ====== BLE ======
BLEService service("D00D0000-1F2E-4A3B-8C7D-6E5F4A3B2C1D");
// Manette -> Arduino : [joystick X, joystick Y, boutons]
BLECharacteristic manetteChar("D00D0001-1F2E-4A3B-8C7D-6E5F4A3B2C1D",
                              BLEWrite | BLEWriteWithoutResponse, 3);
// Arduino -> iPhone (vibrations) : 1 = touché, 2 = monstre touché, 3 = monstre tué, 4 = game over, 5 = victoire
BLEByteCharacteristic retourChar("D00D0002-1F2E-4A3B-8C7D-6E5F4A3B2C1D", BLERead | BLENotify);

// ====== État du jeu ======
enum Etat { JEU, MORT, VICTOIRE };
Etat etat = JEU;


float posX, posY, dirX, dirY, planX, planY;
int sante = 100;
int tues = 0;

int8_t joyX = 0, joyY = 0;
uint8_t boutons = 0;
bool tirAvant = false;
bool etaitConnecte = false;

// Colonnes : image actuelle et précédente
int16_t  hautCol[NB_RAYONS], basCol[NB_RAYONS];
uint16_t couleurCol[NB_RAYONS];
float    zbuf[NB_RAYONS];
int16_t  ancienHaut[NB_RAYONS], ancienBas[NB_RAYONS];
uint16_t ancienneCouleur[NB_RAYONS];

int ancienJoueurX = -1, ancienJoueurY = -1;
unsigned long dernierTemps = 0, dernierTir = 0, santeFlashJusqua = 0;
bool flashArme = false, santeFlash = false;
bool forceRendu = true;

unsigned long debutFps = 0;
int images = 0;

// ---------- Outils ----------

uint8_t carte(int x, int y) {
  if (x < 0 || y < 0 || x >= CARTE_T || y >= CARTE_T) return 1;
  return CARTE[y][x];
}

bool libre(float x, float y) {
  return carte((int)x, (int)y) == 0;
}

uint16_t ombrer(uint16_t c, uint16_t f) {
  uint16_t r = (((c >> 11) & 0x1F) * f) >> 8;
  uint16_t g = (((c >> 5) & 0x3F) * f) >> 8;
  uint16_t b = ((c & 0x1F) * f) >> 8;
  return (r << 11) | (g << 5) | b;
}

void bande(int x, int y0, int y1, uint16_t c) {
  if (y0 < 0) y0 = 0;
  if (y1 > VUE_H) y1 = VUE_H;
  if (y1 > y0) gfx->fillRect(x, y0, COL_L, y1 - y0, c);
}

// Remplit [y0, y1) avec ce qu'il y a "derrière" : plafond, mur ou sol
void fondBande(int x, int y0, int y1, int haut, int bas, uint16_t mur) {
  if (y1 <= y0) return;
  bande(x, max(y0, 0),    min(y1, haut),  PLAFOND);
  bande(x, max(y0, haut), min(y1, bas),   mur);
  bande(x, max(y0, bas),  min(y1, VUE_H), SOL);
}

void texte(int x, int y, const char* s, uint8_t taille, uint16_t couleur, uint16_t fond) {
  gfx->setTextSize(taille);
  gfx->setTextColor(couleur, fond);
  gfx->setCursor(x, y);
  gfx->print(s);
}

// Ligne de vue entre deux points (pas de mur entre les deux)
bool voit(float x0, float y0, float x1, float y1) {
  float dx = x1 - x0, dy = y1 - y0;
  int n = (int)(sqrtf(dx * dx + dy * dy) / 0.2f) + 1;
  for (int k = 1; k < n; k++) {
    float t = (float)k / n;
    if (!libre(x0 + dx * t, y0 + dy * t)) return false;
  }
  return true;
}

void envoyerRetour(uint8_t code) {
  if (BLE.connected()) retourChar.writeValue(code);
}

void toutRedessiner() {
  for (int i = 0; i < NB_RAYONS; i++) ancienHaut[i] = -1;
  forceRendu = true;
}

// ---------- Rendu 3D ----------

void calculerMurs() {
  for (int i = 0; i < NB_RAYONS; i++) {
    float camX = 2.0f * i / (NB_RAYONS - 1) - 1.0f;
    float rayX = dirX + planX * camX;
    float rayY = dirY + planY * camX;

    int caseX = (int)posX, caseY = (int)posY;
    float deltaX = (rayX == 0) ? 1e30f : fabsf(1.0f / rayX);
    float deltaY = (rayY == 0) ? 1e30f : fabsf(1.0f / rayY);

    int pasX, pasY;
    float distX, distY;
    if (rayX < 0) { pasX = -1; distX = (posX - caseX) * deltaX; }
    else          { pasX =  1; distX = (caseX + 1.0f - posX) * deltaX; }
    if (rayY < 0) { pasY = -1; distY = (posY - caseY) * deltaY; }
    else          { pasY =  1; distY = (caseY + 1.0f - posY) * deltaY; }

    int cote = 0;
    uint8_t type = 1;
    for (int n = 0; n < 64; n++) {
      if (distX < distY) { distX += deltaX; caseX += pasX; cote = 0; }
      else               { distY += deltaY; caseY += pasY; cote = 1; }
      type = carte(caseX, caseY);
      if (type) break;
    }

    float dist = (cote == 0) ? (distX - deltaX) : (distY - deltaY);
    if (dist < 0.05f) dist = 0.05f;
    zbuf[i] = dist;

    int hauteur = (int)(VUE_H * FACTEUR_HAUTEUR / dist);
    int haut = VUE_H / 2 - hauteur / 2;
    int bas  = VUE_H / 2 + hauteur / 2;
    hautCol[i] = haut < 0 ? 0 : haut;
    basCol[i]  = bas > VUE_H ? VUE_H : bas;

    int niveau = constrain((int)(dist * 1.5f), 0, 7);
    uint16_t facteur = 256 - niveau * 24;
    if (cote == 1) facteur = (facteur * 3) / 4;
    couleurCol[i] = ombrer(COULEURS_MURS[type], facteur);
  }
}

// Calcule où un monstre apparaît à l'écran
bool projeter(int idx, Sprite& s) {
  const Ennemi& e = ennemis[idx];
  float sx = e.x - posX, sy = e.y - posY;
  float invDet = 1.0f / (planX * dirY - dirX * planY);
  float tx = invDet * (dirY * sx - dirX * sy);
  float ty = invDet * (-planY * sx + planX * sy);   // profondeur
  if (ty < 0.2f) return false;                      // derrière le joueur

  s.idx = idx;
  s.prof = ty;
  s.centreX = (int)((LARGEUR / 2) * (1.0f + tx / ty));
  s.hauteur = (int)(VUE_H * FACTEUR_HAUTEUR / ty * TAILLE_MONSTRE);
  s.largeur = (int)((LARGEUR / 2) / (ty * PLAN) * TAILLE_MONSTRE);
  if (s.largeur < 1) s.largeur = 1;
  if (s.hauteur < 1) s.hauteur = 1;
  s.gauche = s.centreX - s.largeur / 2;
  int sol = VUE_H / 2 + (int)(VUE_H * FACTEUR_HAUTEUR / ty) / 2;   // pieds posés au sol
  s.haut = sol - s.hauteur;

  return !(s.gauche + s.largeur < 0 || s.gauche >= LARGEUR);
}

void dessinerColonneSprite(int i, int x, int px, const Sprite& s) {
  int haut = hautCol[i], bas = basCol[i];
  uint16_t mur = couleurCol[i];
  bool flash = millis() < ennemis[s.idx].flashJusqua;

  int niveau = constrain((int)(s.prof * 1.5f), 0, 7);
  uint16_t facteur = 256 - niveau * 24;

  int tx = (px - s.gauche) * 16 / s.largeur;
  tx = constrain(tx, 0, 15);

  fondBande(x, 0, s.haut, haut, bas, mur);

  // On dessine la colonne du monstre par morceaux de même couleur
  int ty = 0;
  while (ty < 16) {
    char t = MONSTRE[ty][tx];
    int debut = ty;
    while (ty < 16 && MONSTRE[ty][tx] == t) ty++;
    int y0 = s.haut + debut * s.hauteur / 16;
    int y1 = s.haut + ty * s.hauteur / 16;
    if (t == ' ') fondBande(x, y0, y1, haut, bas, mur);
    else          bande(x, y0, y1, flash ? BLANC : ombrer(couleurTexel(t), facteur));
  }

  fondBande(x, s.haut + s.hauteur, VUE_H, haut, bas, mur);
}

void dessinerVue() {
  calculerMurs();

  nbSprites = 0;
  for (int k = 0; k < NB_ENNEMIS; k++) {
    if (ennemis[k].vivant && projeter(k, sprites[nbSprites])) nbSprites++;
  }

  for (int i = 0; i < NB_RAYONS; i++) {
    int x = i * COL_L;
    int px = x + COL_L / 2;

    // Le monstre le plus proche devant le mur dans cette colonne
    int meilleur = -1;
    float profMin = 1e9f;
    for (int k = 0; k < nbSprites; k++) {
      const Sprite& s = sprites[k];
      if (px >= s.gauche && px < s.gauche + s.largeur && s.prof < zbuf[i] && s.prof < profMin) {
        meilleur = k;
        profMin = s.prof;
      }
    }

    if (meilleur >= 0) {
      dessinerColonneSprite(i, x, px, sprites[meilleur]);
      ancienHaut[i] = -1;   // colonne à redessiner entièrement la prochaine fois
      continue;
    }

    // Colonne sans monstre : on ne redessine que ce qui a changé
    int haut = hautCol[i], bas = basCol[i];
    uint16_t couleur = couleurCol[i];
    int ah = ancienHaut[i], ab = ancienBas[i];

    if (ah < 0) {
      bande(x, 0, haut, PLAFOND);
      bande(x, haut, bas, couleur);
      bande(x, bas, VUE_H, SOL);
    } else if (couleur != ancienneCouleur[i]) {
      if (haut > ah) bande(x, ah, haut, PLAFOND);
      bande(x, haut, bas, couleur);
      if (bas < ab) bande(x, bas, ab, SOL);
    } else {
      if (haut < ah) bande(x, haut, ah, couleur);
      if (haut > ah) bande(x, ah, haut, PLAFOND);
      if (bas > ab)  bande(x, ab, bas, couleur);
      if (bas < ab)  bande(x, bas, ab, SOL);
    }
    ancienHaut[i] = haut;
    ancienBas[i] = bas;
    ancienneCouleur[i] = couleur;
  }

  // Viseur
  gfx->drawFastHLine(LARGEUR / 2 - 6, VUE_H / 2, 13, BLANC);
  gfx->drawFastVLine(LARGEUR / 2, VUE_H / 2 - 6, 13, BLANC);
  for (int i = (LARGEUR / 2 - 6) / COL_L; i <= (LARGEUR / 2 + 6) / COL_L; i++) ancienHaut[i] = -1;
}

// ---------- Barre du bas ----------

void dessinerArme(bool flash) {
  gfx->fillRect(190, 242, 100, 78, HUD_FOND);
  if (flash) {
    gfx->fillCircle(240, 254, 12, JAUNE);
    gfx->fillCircle(240, 254, 6, BLANC);
  }
  gfx->fillRect(232, 262, 16, 30, GRIS);
  gfx->fillRect(234, 262, 4, 30, GRIS_CLAIR);
  gfx->fillRect(214, 290, 52, 30, GRIS_FONCE);
  gfx->fillRect(226, 296, 28, 6, GRIS);
}

void dessinerSante(bool touche) {
  uint16_t fond = touche ? ROUGE : HUD_FOND;
  gfx->fillRect(12, 272, 80, 36, fond);
  char txt[6];
  snprintf(txt, sizeof(txt), "%3d", sante);
  texte(16, 276, txt, 4, touche ? BLANC : ROUGE, fond);
}

void dessinerCompteur() {
  char txt[12];
  snprintf(txt, sizeof(txt), "RESTE %d", NB_ENNEMIS - tues);
  texte(296, 280, txt, 2, GRIS, HUD_FOND);
}

void dessinerEtatBLE() {
  if (BLE.connected()) texte(296, 252, "BLE OK", 2, VERT, HUD_FOND);
  else                 texte(296, 252, "BLE --", 2, GRIS, HUD_FOND);
}

void dessinerCaseCarte(int cx, int cy) {
  if (cx < 0 || cy < 0 || cx >= CARTE_T || cy >= CARTE_T) return;
  uint8_t t = CARTE[cy][cx];
  uint16_t c = t ? ombrer(COULEURS_MURS[t], 160) : CARTE_VIDE;
  gfx->fillRect(MINI_X + cx * MINI_CASE, MINI_Y + cy * MINI_CASE, MINI_CASE, MINI_CASE, c);
}

void dessinerJoueurCarte() {
  int px = MINI_X + (int)(posX * MINI_CASE) - 1;
  int py = MINI_Y + (int)(posY * MINI_CASE) - 1;
  if (px == ancienJoueurX && py == ancienJoueurY) return;
  if (ancienJoueurX >= 0) {
    for (int cy = (ancienJoueurY - MINI_Y) / MINI_CASE; cy <= (ancienJoueurY + 2 - MINI_Y) / MINI_CASE; cy++)
      for (int cx = (ancienJoueurX - MINI_X) / MINI_CASE; cx <= (ancienJoueurX + 2 - MINI_X) / MINI_CASE; cx++)
        dessinerCaseCarte(cx, cy);
  }
  gfx->fillRect(px, py, 3, 3, ROUGE);
  ancienJoueurX = px;
  ancienJoueurY = py;
}

void dessinerHUD() {
  gfx->fillRect(0, VUE_H, LARGEUR, 320 - VUE_H, HUD_FOND);
  gfx->drawFastHLine(0, VUE_H, LARGEUR, HUD_BORD);
  texte(16, 252, "SANTE", 2, GRIS, HUD_FOND);
  dessinerSante(false);
  dessinerCompteur();
  dessinerEtatBLE();
  dessinerArme(false);
  for (int y = 0; y < CARTE_T; y++)
    for (int x = 0; x < CARTE_T; x++)
      dessinerCaseCarte(x, y);
  ancienJoueurX = -1;
  dessinerJoueurCarte();
}

// ---------- Écrans ----------

void ecranTitre() {
  gfx->fillScreen(NOIR);
  texte(78, 110, "MINI DOOM", 6, ROUGE, NOIR);
  texte(84, 190, "Connecte la manette iPhone", 2, GRIS, NOIR);
  delay(2000);
}

void ecranFin(const char* titre, uint16_t couleur) {
  gfx->fillRect(0, 0, LARGEUR, VUE_H, NOIR);
  int largeurTitre = strlen(titre) * 36;
  texte((LARGEUR - largeurTitre) / 2, 80, titre, 6, couleur, NOIR);
  texte(78, 170, "Appuie sur TIR pour rejouer", 2, GRIS, NOIR);
  toutRedessiner();
}

void nouvellePartie() {
  posX = 1.5f; posY = 1.5f;
  dirX = 1.0f; dirY = 0.0f;
  planX = 0.0f; planY = PLAN;
  sante = 100;
  tues = 0;
  for (int k = 0; k < NB_ENNEMIS; k++) {
    ennemis[k].x = DEPARTS_ENNEMIS[k][0];
    ennemis[k].y = DEPARTS_ENNEMIS[k][1];
    ennemis[k].vie = VIE_ENNEMI;
    ennemis[k].vivant = true;
    ennemis[k].alerte = false;
    ennemis[k].dernierCoup = 0;
    ennemis[k].flashJusqua = 0;
  }
  etat = JEU;
  dessinerHUD();
  toutRedessiner();
}

// ---------- Logique ----------

// Fait bouger et attaquer les monstres. Renvoie true si au moins un est actif.
bool mettreAJourEnnemis(float dt, unsigned long maintenant) {
  bool actif = false;
  for (int k = 0; k < NB_ENNEMIS; k++) {
    Ennemi& e = ennemis[k];
    if (!e.vivant) continue;

    float dx = posX - e.x, dy = posY - e.y;
    float d = sqrtf(dx * dx + dy * dy);

    if (!e.alerte && d < DISTANCE_VUE && voit(e.x, e.y, posX, posY)) e.alerte = true;
    if (!e.alerte) continue;
    actif = true;

    // Se rapproche du joueur
    if (d > PORTEE_ATTAQUE * 0.8f) {
      float vx = dx / d * VITESSE_ENNEMI * dt;
      float vy = dy / d * VITESSE_ENNEMI * dt;
      if (libre(e.x + vx + (vx > 0 ? MARGE : -MARGE), e.y)) e.x += vx;
      if (libre(e.x, e.y + vy + (vy > 0 ? MARGE : -MARGE))) e.y += vy;
    }

    // Attaque au corps à corps
    if (d < PORTEE_ATTAQUE && maintenant - e.dernierCoup > CADENCE_ATTAQUE) {
      e.dernierCoup = maintenant;
      sante -= DEGATS;
      if (sante < 0) sante = 0;
      santeFlash = true;
      santeFlashJusqua = maintenant + 200;
      dessinerSante(true);
      envoyerRetour(1);
    }
  }
  return actif;
}

void tirer(unsigned long maintenant) {
  flashArme = true;
  dessinerArme(true);

  // Cherche le monstre le plus proche sous le viseur
  int cible = -1;
  float profMin = 1e9f;
  Sprite s;
  for (int k = 0; k < NB_ENNEMIS; k++) {
    if (!ennemis[k].vivant || !projeter(k, s)) continue;
    bool vise = abs(s.centreX - LARGEUR / 2) < s.largeur * 0.35f;
    if (vise && s.prof < zbuf[NB_RAYONS / 2] && s.prof < profMin) {
      cible = k;
      profMin = s.prof;
    }
  }
  if (cible < 0) return;

  Ennemi& e = ennemis[cible];
  e.vie--;
  e.alerte = true;
  e.flashJusqua = maintenant + 120;
  forceRendu = true;

  if (e.vie <= 0) {
    e.vivant = false;
    tues++;
    dessinerCompteur();
    envoyerRetour(3);
    if (tues == NB_ENNEMIS) {
      etat = VICTOIRE;
      envoyerRetour(5);
      ecranFin("VICTOIRE !", VERT);
    }
  } else {
    envoyerRetour(2);
  }
}

// ---------- Programme principal ----------

void setup() {
  Serial.begin(115200);

  if (!gfx->begin()) Serial.println("Erreur : ecran non detecte");

  if (!BLE.begin()) {
    Serial.println("Erreur : BLE non disponible");
    while (true);
  }
  BLE.setLocalName("Mini Doom");
  BLE.setAdvertisedService(service);
  service.addCharacteristic(manetteChar);
  service.addCharacteristic(retourChar);
  BLE.addService(service);
  retourChar.writeValue(0);
  BLE.advertise();

  ecranTitre();
  gfx->fillScreen(NOIR);
  nouvellePartie();

  dernierTemps = millis();
  debutFps = millis();
}

void loop() {
  BLE.poll();

  if (manetteChar.written() && manetteChar.valueLength() >= 3) {
    const uint8_t* v = manetteChar.value();
    joyX = (int8_t)v[0];
    joyY = (int8_t)v[1];
    boutons = v[2];
  }

  bool connecte = BLE.connected();
  if (connecte != etaitConnecte) {
    etaitConnecte = connecte;
    if (!connecte) { joyX = 0; joyY = 0; boutons = 0; }
    dessinerEtatBLE();
  }

  unsigned long maintenant = millis();
  float dt = (maintenant - dernierTemps) / 1000.0f;
  dernierTemps = maintenant;
  if (dt > 0.1f) dt = 0.1f;

  bool tir = boutons & 1;
  bool nouveauTir = tir && !tirAvant;
  tirAvant = tir;

  // Écran de fin : on attend un appui sur TIR
  if (etat != JEU) {
    if (nouveauTir) nouvellePartie();
    return;
  }

  bool bouge = false;
  int jx = abs(joyX) < ZONE_MORTE ? 0 : joyX;
  int jy = abs(joyY) < ZONE_MORTE ? 0 : joyY;

  if (jx != 0) {
    float a = jx / 100.0f * VITESSE_ROT * dt;
    float c = cosf(a), s = sinf(a);
    float ancienDirX = dirX;
    dirX = dirX * c - dirY * s;
    dirY = ancienDirX * s + dirY * c;
    float ancienPlanX = planX;
    planX = planX * c - planY * s;
    planY = ancienPlanX * s + planY * c;
    bouge = true;
  }

  if (jy != 0) {
    float d = jy / 100.0f * VITESSE * dt;
    float mx = dirX * d, my = dirY * d;
    if (libre(posX + mx + (mx > 0 ? MARGE : -MARGE), posY)) posX += mx;
    if (libre(posX, posY + my + (my > 0 ? MARGE : -MARGE))) posY += my;
    bouge = true;
  }

  // Tir (maintenir le bouton = tir automatique)
  if (tir && maintenant - dernierTir > CADENCE_TIR) {
    dernierTir = maintenant;
    tirer(maintenant);
    if (etat != JEU) return;
  }
  if (flashArme && maintenant - dernierTir > 100) {
    flashArme = false;
    dessinerArme(false);
  }

  bool monstresActifs = mettreAJourEnnemis(dt, maintenant);

  if (santeFlash && maintenant > santeFlashJusqua) {
    santeFlash = false;
    dessinerSante(false);
  }

  if (sante <= 0) {
    etat = MORT;
    envoyerRetour(4);
    ecranFin("GAME OVER", ROUGE);
    return;
  }

  // Rendu seulement si quelque chose bouge
  if (bouge || monstresActifs || forceRendu) {
    dessinerVue();
    dessinerJoueurCarte();
    forceRendu = false;
    images++;
  }

  if (maintenant - debutFps >= 1000) {
    if (images > 0) {
      Serial.print("FPS : ");
      Serial.println(images);
    }
    images = 0;
    debutFps = maintenant;
  }
}
