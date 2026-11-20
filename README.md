# oraclee  ·  v2.0.0

**oraclee** transforme un sample audio en MIDI : batterie, basse, accords et mélodie séparés, vélocités, MIDI calé sur le BPM de ta session, et un piano roll façon FL Studio pour retoucher les notes avant de les glisser dans ton DAW.

Formats : **VST3**, **AU** (macOS) et **appli autonome**.

## Fonctionnalités

- **Glisser-déposer partout**
  - Entrée : glisse un sample audio (WAV, AIFF, MP3, FLAC, OGG, M4A) ou un fichier `.mid` dans la fenêtre.
  - Sortie : glisse une piste (Mélodie, Accords, Basse, Batterie, ou Tout), ou ta sélection de notes, directement sur une piste de ton DAW.
- **Séparation des instruments** : kick, snare, hi-hat, basse, accords et mélodie, chacun sur sa piste MIDI.
- **Détection précise**
  - Tempo du sample et accordage global (samples désaccordés) détectés automatiquement.
  - Bon calage des attaques, et notes répétées distinguées des notes tenues.
- **Calé sur la session** : le MIDI suit le tempo de ton projet ; le tempo du sample est rapproché automatiquement (×2 ou ÷2 si besoin).
- **Synthé adaptatif** : la lecture rejoue le MIDI avec des sons appris du sample (une note propre échantillonnée et retransposée, sinon un timbre modélisé : harmoniques, attaque, décroissance, relâchement ; batterie avec les vrais coups découpés). Bouton **ORIGINAL / RECRÉÉ** pour comparer.
- **Vélocités** issues de la force d'attaque de chaque note, retouchables dans la ligne de vélocité.
- **Piano roll** (inspiré de FL Studio) avec lecteur interne.
- **Jouer avec le DAW** : pendant la lecture du projet, oraclee envoie son MIDI en direct, un canal par instrument.

## Piano roll : commandes

| Action | Commande |
|---|---|
| Créer une note | Crayon (P) : clic dans le vide (la note suit la souris) |
| Déplacer | glisser une note (aimantée à la grille) |
| Changer la durée | glisser le bord droit |
| Dupliquer en glissant | Maj + glisser |
| Effacer | clic droit, ou clic droit maintenu en balayant |
| Sélectionner un groupe | outil Sélection (E), ou Cmd/Ctrl + glisser |
| Tout sélectionner | Cmd/Ctrl + A |
| Transposer | ↑ ↓ (Maj = octave) |
| Décaler dans le temps | ← → |
| Dupliquer à la suite | Cmd/Ctrl + D |
| Copier / coller au curseur | Cmd/Ctrl + C / V |
| Quantize | Q |
| Annuler / rétablir | Cmd/Ctrl + Z / Cmd/Ctrl + Maj + Z |
| Lecture / stop | Espace |
| Placer le curseur | clic dans la règle |
| Vélocités | glisser dans la ligne « Vélocité » (seulement les notes sélectionnées s'il y en a) |
| Écouter une hauteur | clic sur le clavier |
| Défiler | molette (hauteur), Maj + molette (temps) |
| Zoomer | Cmd/Ctrl + molette, pincement du trackpad, Alt + molette (hauteur) |

« Piste éditée » choisit l'instrument que tu modifies ; les autres pistes restent visibles en transparence (« Fantômes »). Les retouches sont enregistrées avec le projet du DAW. Un « Réanalyser » reste annulable avec Cmd/Ctrl + Z.

## Router le MIDI dans le DAW (mode « Jouer avec le DAW »)

| Instrument | Canal |
|---|---|
| Mélodie | 1 |
| Accords | 2 |
| Basse | 3 |
| Batterie (kick 36, snare 38, hi-hat 42) | 10 |

- **Ableton Live** : sur une piste MIDI, règle *MIDI From* sur la piste oraclee et choisis le canal, puis mets *Monitor* sur *In*.
- **FL Studio** : dans le wrapper du plugin (*Settings*), règle un port de sortie MIDI, puis mets le même port en entrée sur l'instrument cible.

## Compiler

Pré-requis :
- **macOS** : outils en ligne de commande Xcode, CMake et (en option) Ninja.
- **Windows** : Visual Studio 2022 et CMake.

```bash
git clone --recursive <url-du-dépôt>
cd oraclee
./build.sh
```

Sur macOS, le script installe `~/Library/Audio/Plug-Ins/Components/oraclee.component` (AU) et `~/Library/Audio/Plug-Ins/VST3/oraclee.vst3`. L'appli autonome se trouve dans `build/Oraclee_artefacts/Release/Standalone/`.

À chaque envoi, GitHub Actions compile les versions macOS (Apple Silicon + Intel) et Windows. Un tag `v*` (par exemple `v2.0.0`) publie une release avec les fichiers .zip.

## Ligne de commande et tests

```bash
build/sample2midi_artefacts/Release/oraclee-cli boucle.wav boucle.mid multi 120 0.5 16
# <audio> <sortie.mid> [multi|mono|poly] [BPM session] [sensibilité 0-1] [quantize 0|4|8|16|32]
```

Outils de développement (activés avec `-DS2M_BUILD_SNAPSHOT=ON`) :

| Outil | Rôle |
|---|---|
| `s2m_edittest` | tests automatiques du piano roll (souris et clavier) |
| `s2m_hostload` | charge le VST3 et l'AU installés comme un DAW et vérifie la sortie MIDI |
| `s2m_snapshot` | rendu de l'interface en PNG |
| `Tools/bench/` | banc de précision noté (F1 par instrument) |

Lancer le banc de précision :

```bash
python3 Tools/bench/gen.py /tmp/bench
python3 Tools/bench/score.py /tmp/bench build/sample2midi_artefacts/Release/oraclee-cli
```

## Comment ça marche, et limites

- L'analyse se fait par traitement du signal, sans IA :
  - séparation harmonique / percussive ;
  - basse suivie en monophonique (YIN) ;
  - estimation polyphonique itérative pour le reste ;
  - détection de l'accordage, du tempo et des attaques ;
  - répartition des notes par registre.
- Score F1 sur le banc de test (4 boucles multi-instruments) : batterie 0,95, basse 0,88, accords 0,80, mélodie 0,79, tempo 4/4.
- Deux instruments dans le même registre (piano et guitare, par exemple) finissent sur la même piste. Pour une séparation par timbre, sépare d'abord les stems, puis passe chaque stem dans oraclee.

## Licence

oraclee est distribué sous licence **GNU AGPLv3** (voir [LICENSE](LICENSE)), la licence open source de [JUCE 8](https://juce.com) sur lequel il est construit.
