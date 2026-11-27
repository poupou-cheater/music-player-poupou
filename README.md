# StreamPulse

Lecteur audio & vidéo moderne en **C++20**, **Vulkan** et **Dear ImGui**, compatible **YouTube Music**, **YouTube**, **TikTok** et fichiers **locaux**.

Dépôt officiel : [https://github.com/poupou-cheater/music-player-poupou](https://github.com/poupou-cheater/music-player-poupou.git)

---

## Fonctionnalités principales

- **Multi-plateformes** :
  - Import de playlists et liens directs (**YouTube Music**, **YouTube**, **TikTok**) via `yt-dlp` + `ffmpeg`.
  - Synchronisation automatique au lancement et toutes les 30 minutes (avec conservation des favoris, fichiers locaux et réglages).
  - Lecture locale (`.mp3`, `.m4a`, `.wav`, `.mp4`, `.mkv`, `.webm`) avec pochette personnalisée.
  - Mode **Hors Ligne** avec téléchargements locaux et nettoyage automatique du cache orphelin.
- **Interface & Modes d'affichage** :
  - **Mode Desktop** : barre latérale avec `Titres Favoris`, `Offline Downloads`, `PLAYLISTS FAVORITES` et `PLATEFORMES & PLAYLISTS`, vue `Liste` ou `Grandes Cartes`.
  - **Mode Focus** (clic sur une zone vide de la barre du bas ou raccourci configurable) : lecteur immersif avec file d'attente rétractable et auto-hide configurable (`Top bar` / `Bottom bar`).
  - **Mode Téléphone (Mobile)** : bascule automatique sous `720 px` ou manuelle dans `Paramètres > Général`, avec lecteur mobile et file d'attente glissante.
  - **Thèmes** intégrés + créateur de thèmes personnalisés avec aperçu en temps réel.
- **Mélangeur Audio & DSP en direct** :
  - Égaliseur 5 bandes (`60 Hz`, `230 Hz`, `910 Hz`, `3.6 kHz`, `14 kHz`), Préampli, Balance G/D, Largeur Stéréo, Mode Mono, Limiteur doux (`tanh`) et profils audio personnalisés.
- **Intégration système** :
  - Touches média globales Windows, Discord Rich Presence, raccourcis clavier personnalisables (clic gauche pour assigner, clic droit pour débind), plein écran `F11`.
  - Vérification automatique des mises à jour sur GitHub au lancement (désactivable dans `Paramètres > Général`).

---

## Compilation (Windows)

### Prérequis
- **CMake** (3.20+)
- **Visual Studio 2022** (Desktop development with C++)
- **Vulkan SDK**
- `tools/yt-dlp.exe` et `tools/ffmpeg.exe` (placés dans `tools/` pour le streaming et le téléchargement)

### Commandes

```powershell
cmake -B build -G "Visual Studio 17 2022"
cmake --build build --config Release -j 8
```

L'exécutable généré se trouve dans `build\Release\StreamPulse.exe`.
