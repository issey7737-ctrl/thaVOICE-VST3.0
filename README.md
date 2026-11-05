# ThaVOICE (VST3)

Suite vocale tout-en-un : AUTOTUNE, COMP (2 étages + parallèle), EQ (analyseur + courbe), FX (de-esser, saturation, doubleur, chorus, téléphone, largeur), DLY (sync tempo, ping-pong, ducking), REV (Room / Plate / Hall / Spring, ducking), 22 presets, A/B, Reset, Bypass.
Deux vues : SIMPLE (un grand bouton COMP + quelques réglages par rangée, Auto input) et PRO (tout le détail).

## Compiler (sans rien installer)
1. Mets le contenu de ce dossier dans ton dépôt GitHub (dossier `YazaVox`, `CMakeLists.txt`, `.github/workflows/build.yml`).
2. Onglet Actions : le build démarre, attends le vert (5 à 10 min).
3. Télécharge l'artefact `ThaVOICE-VST3-Windows`, copie `ThaVOICE.vst3` dans `C:\Program Files\Common Files\VST3`.
4. FL Studio : Options > Manage plugins > Find plugins.

## Autotune
- Active TUNE, choisis la tonalité (KEY) et la gamme (SCALE).
- SPEED : 100 % = effet dur « autotune », 30-40 % = correction naturelle. AMOUNT : force de la correction.
- Le décalage de hauteur utilise la bibliothèque open source **Signalsmith Stretch** (licence MIT, dossier `YazaVox/third_party`), la détection de note est un algorithme YIN maison.
- Latence fixe de 90 ms, déclarée à FL Studio (compensation automatique). Pour enregistrer en direct, mets plutôt TUNE sur la piste après l'enregistrement.

## Test du moteur (optionnel)
`YazaVox/tests_yv.cpp` : tests hors-ligne (44,1 / 48 / 88,2 / 96 kHz, blocs 128 à 2048).
