# Voice Cleaner — guide d'installation pas à pas

Voice Cleaner est un plugin **VST3 pour Windows** qui fait deux choses sur ta voix :

1. **Anti-larsen automatique** : il repère les sifflements et les atténue tout seul.
2. **Débruitage par IA** (modèle RNNoise) : il enlève souffle, ventilateur, bruit de pièce.

Il fonctionne dans **Ableton Live** et **DaVinci Resolve (Fairlight)**.
Tu n'as rien à programmer. Tu vas simplement faire compiler le plugin gratuitement par GitHub.

---

## Partie 1 — Faire fabriquer le plugin (environ 20 minutes, une seule fois)

### Étape 1 : créer un compte GitHub (gratuit)
1. Va sur **github.com** et clique sur **Sign up**.
2. Crée ton compte (e-mail, mot de passe, nom d'utilisateur) et confirme ton e-mail.

### Étape 2 : créer un dépôt privé
Un « dépôt » est un dossier en ligne. Le tien sera privé : toi seul le vois.
1. Une fois connecté, clique sur le **+** en haut à droite, puis **New repository**.
2. Dans **Repository name**, écris `voice-cleaner`.
3. Choisis **Private**.
4. Ne coche rien d'autre, puis clique sur **Create repository**.

### Étape 3 : envoyer les fichiers
1. Dézippe le dossier `VoiceCleaner.zip` sur ton PC (clic droit > **Extraire tout**).
2. Sur la page de ton dépôt vide, clique sur le lien **uploading an existing file**.
3. Ouvre le dossier dézippé. **Sélectionne tout son contenu** (Ctrl+A) et glisse-le dans la page GitHub.
   Il doit y avoir : `CMakeLists.txt`, le dossier `Source`, et le dossier `.github`.
4. Attends la fin du chargement, puis clique sur le bouton vert **Commit changes**.

> **Si le dossier `.github` n'apparaît pas** (il est parfois ignoré au glisser-déposer) :
> 1. Sur la page du dépôt, clique sur **Add file > Create new file**.
> 2. Dans le nom du fichier, tape exactement `.github/workflows/build.yml`
>    (en tapant les `/`, GitHub crée les dossiers).
> 3. Ouvre le fichier `build.yml` de ton dossier avec le Bloc-notes, copie tout son contenu et colle-le dans GitHub.
> 4. Clique sur **Commit changes**.

### Étape 4 : lancer la fabrication
1. Clique sur l'onglet **Actions** en haut de ton dépôt.
2. Si GitHub demande d'activer les workflows, clique sur le bouton vert pour les activer.
3. Tu dois voir une ligne **Compiler le plugin Windows** avec un cercle orange qui tourne.
   Sinon : clique sur **Compiler le plugin Windows** à gauche, puis **Run workflow** > **Run workflow**.
4. Attends 10 à 25 minutes (le cercle devient une coche verte).

### Étape 5 : télécharger le plugin
1. Clique sur la ligne terminée (avec la coche verte).
2. Tout en bas, dans **Artifacts**, clique sur **Voice-Cleaner-Windows**. Un fichier .zip se télécharge.
3. Dézippe-le : tu obtiens un dossier **Voice Cleaner.vst3** (et un petit programme `Voice Cleaner.exe` pour tester seul).

### Si une croix rouge apparaît à la place de la coche verte
C'est possible, car je n'ai pas pu compiler le code de mon côté. Ce n'est pas grave :
1. Clique sur la ligne en erreur, puis sur **build-windows**.
2. Clique sur l'étape marquée d'une croix rouge pour la déplier.
3. Sélectionne les dernières lignes de texte rouge, copie-les et **colle-les-moi dans le chat**.
   Je corrige les fichiers et te dis lesquels remplacer, puis on relance (étape 4).

---

## Partie 2 — Installer le plugin sur ton PC

1. Copie le dossier **Voice Cleaner.vst3** dans :
   `C:\Program Files\Common Files\VST3`
   (Windows demande une autorisation administrateur : accepte.)
2. Redémarre ton logiciel audio.

### Dans Ableton Live
1. **Options > Préférences > Plug-ins** (Live 11/12 : *Settings > Plug-ins*).
2. Active **Use VST3 System Folders** (utiliser les dossiers système VST3).
3. Clique sur **Rescan** (rescanner).
4. Dans le navigateur à gauche : **Plug-ins > Voice Cleaner**. Glisse-le sur ta piste voix.

### Dans DaVinci Resolve (Fairlight)
1. **DaVinci Resolve > Préférences > Système > Audio Plugins** (le chemin exact peut changer un peu selon la version).
2. Vérifie que **Voice Cleaner** est dans la liste, active-le (case cochée), puis **Actualiser / Refresh** si besoin.
   S'il n'apparaît pas, redémarre Resolve.
3. Dans la page **Fairlight**, ouvre le mélangeur, clique dans la zone **Effects** d'une piste, choisis **Voice Cleaner**.

---

## Partie 3 — Les réglages

| Réglage | À quoi il sert |
|---|---|
| **Anti-larsen** | Active / coupe la suppression automatique des sifflements. |
| **Debruitage IA** | Active / coupe le débruitage. |
| **Force debruitage (%)** | 100 % = débruitage maximal. Baisse (par ex. 70 %) si la voix sonne un peu métallique ou « sous l'eau ». |
| **Larsen : seuil de détection** | Plus il est **bas**, plus le plugin réagit vite (mais risque de toucher la voix). Plus il est **haut**, moins il réagit. Départ conseillé : 15. |
| **Larsen : coupure max** | Jusqu'à combien de dB il a le droit de creuser une fréquence qui siffle. |
| **Larsen : finesse (Q)** | Plus c'est haut, plus la coupure est étroite (donc discrète). |
| **Larsen : relâchement** | Vitesse à laquelle il « relâche » une coupure quand le sifflement a disparu. |

---

## Ce qu'il faut savoir

- **Latence : environ 20 ms** (un cinquantième de seconde). Le plugin l'annonce à ton logiciel, qui la compense tout seul au montage. Pour t'écouter en direct dans un casque, ce retard peut être perceptible.
- Le débruitage est **conçu pour la voix**. Sur de la musique, il peut abîmer le son.
- Pas encore d'interface graphique personnalisée : tu as des curseurs et des boutons simples fournis par le logiciel.
- Ce plugin est pour **ton usage personnel**.
