# StreamPulse — Avancement & Règles UI / Build

## Fonctionnalités implémentées (état actuel)
- **Barre du haut (`renderTopBar` dans `src/ui/AppUI.cpp`)** :
  - Navigation (`Accueil`, `YT Music`, `TikTok`, `File(...)`, `Downloads`, `Stats`), sélecteur de thème, paramètres, badge réseau (`En Ligne` / `Hors Ligne`) et bouton compte icône seule (`##top_account`, `assets/icons/account.svg`).
- **Comptes & Authentification (`PlatformSync` + `Models.hpp` + `LibraryManager.cpp`)** :
  - Connexion **YouTube Music** et **TikTok** via cookies navigateur (`firefox`, `edge`, `chrome`, `brave`, `opera`) ou fichier `cookies.txt`, test en direct (`testAccountConnectionAsync`), et import rapide (playlists privées, Titres Likés `LM`, Likes/Favoris/Collections TikTok).
  - Injection automatique de `--cookies-from-browser` / `--cookies` via `buildCookieArgs` / `buildCookieArgsForUrl` dans `PlatformSync.cpp`.
- **Barre latérale (`renderSidebar` dans `src/ui/AppUI.cpp`)** :
  - `Titres Favoris`, `Offline Downloads`, `PLAYLISTS FAVORITES` et `PLATEFORMES & PLAYLISTS` (les catégories vides ne sont pas affichées).
  - Clic droit sur une playlist : favoris, synchronisation, téléchargement, options (`Auto-Sync`, `Auto-Download`, `Préférer Vidéo`), `Audio / dB & EQ`, renommer, supprimer fichiers locaux, supprimer la playlist.
  - Clic droit sur une zone vide de la sidebar (`##SidebarEmptyCtx`) : créer une nouvelle playlist (`###CreatePlPopup`).
  - Bouton `+ Importer` fixé tout en bas de la sidebar (zone `##SidebarScroll` au-dessus).
- **Modal Importer (`renderImportModal` dans `src/ui/AppUI.cpp`)** :
  - Sans barre de titre (`ImGuiWindowFlags_NoTitleBar`) et sans filtre blanc.
  - 3 onglets simples : **Playlist**, **Link**, **Local** (avec sélecteur de fichier Win32 `browseFileWin32`).
- **Barre de lecture en bas (`renderPlayerBar` dans `src/ui/AppUI.cpp`)** :
  - Boutons icônes centrés verticalement et sans bordure, bouton cœur aligné à droite du titre/artiste, contrôles de droite alignés à X fixe (`%3.0f%%`).

---

## Règles à respecter pour ne pas réintroduire d'erreurs
1. **Pas de filtre blanc sur les modals** (`src/ui/Templates.cpp`) :
   - Garder `c[ImGuiCol_ModalWindowDimBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);` dans `applyPaletteStyle`.
2. **Visibilité des champs texte & boutons dans les popups** (`src/ui/Templates.cpp`) :
   - Garder `s.FrameBorderSize = 1.0f;` et `s.PopupBorderSize = 1.0f;`.
   - Ne jamais mettre la même couleur pour `ImGuiCol_PopupBg`, `ImGuiCol_FrameBg` et `ImGuiCol_Button`.
3. **Boutons icônes sans bordure** (`src/ui/IconStore.cpp`) :
   - `IconStore::button` doit encadrer `ImGui::ImageButton` avec `ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f)` / `PopStyleVar()`, sinon tous les boutons de la barre du bas récupèrent une bordure carrée.
4. **Sidebar sans double scrollbar** (`src/ui/AppUI.cpp`) :
   - `##Sidebar` doit garder `ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse`.
   - `##SidebarScroll` doit réserver la hauteur du pied (`GetContentRegionAvail().y - 48.0f`) pour que `+ Importer` reste collé tout en bas.
5. **Windows headers dans `AppUI.cpp`** :
   - Inclure `<windows.h>` et `<commdlg.h>` (avec `NOMINMAX`) après `<GLFW/glfw3.h>` pour `OPENFILENAMEW` / `GetOpenFileNameW`.
6. **Vérification après modification** :
   - Fermer `StreamPulse.exe` avant de recompiler (`Stop-Process -Name "StreamPulse" -Force -ErrorAction SilentlyContinue`), compiler en `Release` avec CMake VS 2022, puis vérifier que `build\Release\StreamPulse.exe` reste actif 4 secondes (`AliveAfter4s=True`).
7. **Règle d'animation globale (Slide In / Slide Out)** :
   - Tout élément UI qui apparaît ou disparaît (panneau latéral droit `##StagePanel`, Mode Focus `##FullscreenStage`, `##TopBar`, `##PlayerBar`, ou tout futur panneau/tiroir) doit utiliser une animation fluide `slide in` / `slide out` avec lissage cubique (`1 - (1 - t)^3`), jamais d'apparition ou disparition instantanée.

---

## Zones figées (NE PAS MODIFIER sauf demande explicite de l'utilisateur)

### 1. Barre de lecture en bas (`renderPlayerBar` — `src/ui/AppUI.cpp`)
- **Gauche** : boutons `Prev` (`26px`), `Play/Pause` (`34px`), `Next` (`26px`) sans bordure (`IconStore::button` avec `FrameBorderSize = 0.0f`) + temps `position / durée`.
- **Centre** : pochette + titre/artiste (`##BarMeta` dimensionné via `CalcTextSize`) + bouton cœur (`22px`) collé juste à droite du texte.
- **Droite** : slider de volume + pourcentage fixe `%3.0f%%` + boutons `Mute`, `Repeat`, `Shuffle`, `Fullscreen` (`24px`) positionnés à X fixe (`rx`) pour ne jamais bouger selon le nombre de chiffres du volume.

### 2. Bloc catégories / Sidebar (`renderSidebar` — `src/ui/AppUI.cpp`)
- **Conteneur externe** : `##Sidebar` avec `ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse`.
- **Zone défilante (`##SidebarScroll`, hauteur `GetContentRegionAvail().y - 48.0f`)** :
  1. `Titres Favoris` et `Offline Downloads`
  2. `PLAYLISTS FAVORITES` (affichée seulement si au moins 1 playlist est favorite)
  3. `PLATEFORMES & PLAYLISTS` (catégories vides masquées)
  4. Menu clic droit sur playlist (`##sb_ctx_` / `##sb_fav_ctx_`) pour toutes les actions, et clic droit sur zone vide (`##SidebarEmptyCtx`) pour `Nouvelle playlist`.
- **Pied fixe tout en bas** : séparateur + bouton pleine largeur `+ Importer` (`ImVec2(-1.0f, 30.0f)`), sans scrollbar externe.
