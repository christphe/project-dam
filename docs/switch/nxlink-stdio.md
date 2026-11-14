# Tester sur la console avec nxlink et récupérer le log tout seul

Objectif : envoyer un NRO de test à la console par le réseau, le laisser démarrer, et recevoir sur
le PC son log et son crash dump, sans copier de carte SD ni coller de rapport à la main.

## Comment nxlink marche (relevé dans les sources)

Sources : `switch-tools/src/nxlink.c`, `nx-hbmenu/common/netloader.c`, `libnx/nx/source/runtime/nxlink.c`.

1. Le PC se connecte en TCP au port **28280** de la console (le netloader de sphaira ou de hbmenu) et
   envoie : longueur + nom du fichier, taille, le NRO compressé (zlib, blocs de 16 Ko), puis le bloc
   d'arguments (chaînes terminées par `\0`, précédé de sa longueur). Le netloader lance le NRO aussitôt.
2. Avec `-s`, le PC écoute ensuite sur le port **28771** (sans délai) : **c'est la Switch qui se
   connecte au PC**, pas l'inverse. Tout ce que le programme écrit sur stdout/stderr arrive là.
3. La console connaît l'adresse du PC par le **dernier argument de `argv`**, de la forme
   `<IPv4 du PC en 8 chiffres hexa>_NXLINK_` (par exemple `0a00a8c0_NXLINK_` pour 192.168.0.10 : les
   octets de l'adresse lus comme un entier little-endian). `nxlinkSetup()` de libnx le retire de `argv`
   et le range dans `__nxlink_host`. Le client officiel n'ajoute pas ce marqueur : c'est le netloader
   de la console qui le fait avec l'adresse de l'expéditeur. Notre client l'ajoute lui-même, au cas où
   (en double, il ne gêne pas : seul le dernier est lu).
4. Côté programme, il faut **initialiser les sockets d'abord** : `socketInitializeDefault()` puis
   `nxlinkStdio()`. Sans les sockets, `nxlinkStdio()` échoue sans bruit et rien n'arrive.
   `nxlinkStdio()` est une fonction `inline` (elle appelle `nxlinkConnectToHost(true, true)`) : on ne
   peut pas la déclarer en `extern` (erreur d'édition de liens), il faut passer par `<switch.h>`.

sphaira ouvre un hôte nxlink automatiquement une fois l'option activée, comme le netloader de
hbmenu : il n'y a rien à lancer à la main sur la console avant chaque envoi.

## Le build de test

`cmake -S . -B build-switch-link -G Ninja -DROMID=ntsc-final -DNX_LINKSTDIO=ON -DCMAKE_TOOLCHAIN_FILE=/opt/devkitpro/cmake/Switch.cmake`
(dossier séparé, comme `build-switch-args` ; l'option est désactivée par défaut et **ce NRO ne se
distribue pas**). Avec `NX_LINKSTDIO` :

- `main()` appelle `nxLinkStdio()` (`port/src/switch/nxmem.c`) en premier : sockets, flux nxlink, et
  le log du jeu (qui écrit aussi sur stderr) arrive sur le PC ;
- le crash handler (`nxcrash.c`) écrit aussi le dump (pc, lr, far, registres, backtrace avec les
  décalages dans le NRO) sur le flux, en plus de `ge007-crash.log` ;
- un thread ferme le jeu après 30 s (`nxExitProcess`), pour enchaîner des lancements sans toucher la
  manette. Le flux se ferme à la sortie du processus.

Résoudre un décalage du crash dump : `aarch64-none-elf-addr2line -f -C -i -e build-switch-link/ge007.aarch64.elf 0x<nro+offset>`
(dans le conteneur de `compose.yaml`, `--entrypoint sh`).

## Le client PC

`python tools/switch/nxlink.py <ip console> <fichier.nro> <chemin sur la SD> <log de sortie> [args...]`

Il se connecte (en réessayant toutes les 3 s tant que le netloader n'écoute pas), envoie le NRO,
ouvre le port 28771 **avant** l'envoi, écrit le flux dans le fichier de log et s'arrête quand le jeu
quitte. Une série de lancements : une boucle shell qui l'appelle avec un fichier de log par essai.

Pièges :
- un seul client à la fois (le netloader n'accepte qu'une connexion) ; ne pas sonder le port ;
- le `nxlink` du conteneur Docker ne joint pas le réseau local (NAT de Docker Desktop) : client
  Python sur Windows ;
- le client n'attend que 120 s la connexion du jeu après l'envoi ; au-delà, le log n'arrive pas ;
- si rien n'arrive : le jeu a peut-être planté avant `main()`, ou n'a pas d'adresse d'hôte
  (marqueur absent) ou pas de sockets.
