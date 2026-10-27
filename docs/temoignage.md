# Épaulard : trente ans plus tard, redonner la parole à Windows 3.11

Aujourd'hui, j'utilise Linux, GNOME, Orca et une plage braille. Je peux naviguer sur Internet, écrire des documents, administrer des serveurs et utiliser quotidiennement mon ordinateur sans trop me poser la question de son accessibilité.

Tout n'est pas parfait, loin de là. Mais quand je repense à mes débuts en informatique, le chemin parcouru est immense.

Et pourtant, plus de trente ans après, j'ai eu envie de retourner sous MS-DOS et Windows 3.11.

Pas pour abandonner mon environnement actuel. Certainement pas pour remplacer LibreOffice par Word 6.

Mais pour retrouver une époque qui a profondément marqué ma vie.

## Quand accéder à un ordinateur coûtait le prix d'une voiture

Au début des années 1990, être non-voyant et vouloir utiliser un ordinateur relevait presque du parcours du combattant.

Un PC coûtait déjà cher. Mais pour nous, ce n'était que le début.

Il fallait ajouter une synthèse vocale, souvent sous la forme d'un équipement matériel spécialisé, un lecteur d'écran et, éventuellement, une plage braille électronique.

L'ensemble pouvait représenter le prix d'une grosse voiture.

Mes parents ont fait un investissement financier considérable pour me permettre d'accéder à cette technologie.

À l'époque, je ne mesurais probablement pas toute l'ampleur de ce sacrifice. J'étais surtout fasciné par cette machine qui me parlait et qui me permettait enfin de lire, d'écrire et d'explorer par moi-même.

Avec le recul, je réalise ce qu'ils m'ont offert.

Ce n'était pas simplement un ordinateur.

**C'était une forme d'indépendance.**

## Des logiciels développés pour nous

Sous MS-DOS, j'utilisais Sonolect et plusieurs petits programmes développés notamment par des bénévoles du Club Micro-Son.

Je me souviens particulièrement de *Liste*, un shell DOS développé par Robert Agro, et de *Lien*, un lecteur de documents conçu par Jean-Claude Mortimor.

Ces logiciels n'avaient rien de spectaculaire. Ils n'étaient pas connus du grand public et ne figuraient pas dans les magazines informatiques.

Mais pour moi, ils étaient essentiels.

C'étaient mes outils quotidiens. Je n'avais pratiquement que ça.

Ils avaient été conçus pour être utilisables sans écran, au clavier, avec une synthèse vocale. Leurs auteurs connaissaient les besoins des utilisateurs non-voyants.

Aujourd'hui, je regrette d'avoir perdu les disquettes qui les contenaient. Elles ont disparu au fil de mes déménagements, comme tant d'autres objets devenus apparemment inutiles.

Je ne pensais évidemment pas qu'un jour je chercherais désespérément à retrouver ces quelques fichiers.

## L'arrivée de Windows : un nouveau défi

Avec Windows 3.1, tout s'est compliqué.

Sous DOS, les informations étaient principalement textuelles. Un lecteur d'écran pouvait analyser le contenu de la mémoire vidéo et restituer les informations à l'utilisateur.

Avec une interface graphique, il fallait désormais interpréter des fenêtres, des menus, des boutons, des boîtes de dialogue et du texte dessiné à l'écran.

Or Windows 3.1 n'avait pas été conçu pour communiquer ces informations aux technologies d'assistance.

Il n'existait pas d'API d'accessibilité comparable à celles dont nous disposons aujourd'hui.

Les développeurs de lecteurs d'écran devaient donc ruser avec le système : intercepter des appels, analyser les fenêtres, surveiller le clavier et reconstruire tant bien que mal une représentation de ce qui apparaissait à l'écran.

Chaque application pouvait poser de nouveaux problèmes.

Et les solutions étaient parfois de véritables bricolages, aussi ingénieux que fragiles.

À l'époque, je profitais de ces logiciels sans forcément comprendre la complexité de leur fonctionnement.

Aujourd'hui, je commence à mesurer le travail que cela représentait.

## Du simple utilisateur au contributeur du logiciel libre

Au fil des années, l'informatique est devenue bien davantage qu'un outil pour moi. Elle est devenue mon métier, mais aussi une passion.

Je suis aujourd'hui administrateur système et j'utilise Linux quotidiennement, notamment NixOS.

J'ai progressivement découvert ce que le logiciel libre pouvait apporter aux utilisateurs de technologies d'assistance : la possibilité de comprendre comment fonctionnent les outils, de les adapter, de corriger leurs défauts et, surtout, de ne pas dépendre entièrement des décisions d'un éditeur.

Je me suis mis à contribuer, modestement, à différents projets.

J'ai notamment travaillé sur des problèmes d'accessibilité dans GTK, GNOME System Monitor, VSCodium et BRLTTY. Je m'intéresse également de près à Orca, le lecteur d'écran de GNOME, et aux évolutions de l'accessibilité sous Linux.

Je ne suis pas développeur de formation. Mon métier, c'est l'administration des systèmes et des infrastructures. Je sais écrire des scripts, comprendre des problèmes techniques et chercher des solutions, mais développer un lecteur d'écran ou un moteur de synthèse vocale était très loin de mes compétences initiales.

Les outils d'intelligence artificielle m'ont permis d'aller beaucoup plus loin dans des domaines que je n'aurais probablement jamais osé aborder seul.

Ils m'aident à écrire du code, à comprendre des mécanismes complexes et à expérimenter. Cela ne dispense évidemment pas de tester, de vérifier et de comprendre ce que l'on fait.

Mais cela ouvre des possibilités assez extraordinaires.

Et surtout, cela m'a donné envie de redonner vie à des technologies que je croyais définitivement disparues.

## Synthé3 : retrouver une voix que je croyais perdue

Parmi mes souvenirs les plus marquants figure une synthèse vocale française : **Synthé3**, commercialisée à l'époque par Electrel.

Elle fonctionnait notamment avec une carte matérielle dédiée. Sa voix était loin du naturel des synthèses modernes, mais elle avait une identité sonore immédiatement reconnaissable.

C'était une voix que j'avais entendue pendant des milliers d'heures.

Une voix qui m'avait accompagné dans mes premiers pas en informatique.

Lorsque j'ai retrouvé les sources historiques de Synthé3, j'ai eu envie de voir s'il était possible de la faire fonctionner sur une machine actuelle.

Ce qui devait être une petite expérience s'est transformé en un véritable travail de remise en état.

J'ai entrepris, avec l'aide d'outils d'IA, de moderniser le moteur tout en préservant autant que possible son comportement et sa personnalité sonore.

Aujourd'hui, **Synthé3 fonctionne nativement sous Linux**, notamment comme module de Speech Dispatcher, ce qui permet de l'utiliser directement avec Orca.

J'ai également porté cette synthèse sous Windows, sous la forme d'un module pour NVDA.

Et il ne s'agit pas simplement d'une démonstration nostalgique qui prononce trois phrases avant de planter.

**La synthèse est suffisamment fonctionnelle, réactive et stable pour être réellement utilisée dans un environnement professionnel.**

Une voix née à une époque où les ordinateurs avaient besoin de cartes spécialisées pour parler peut désormais accompagner un utilisateur dans son travail quotidien, sous Linux comme sous Windows.

Je trouve cela assez extraordinaire.

D'autant que je ne cherchais pas à remplacer les synthèses modernes. Je voulais simplement éviter qu'une voix qui avait compté dans ma vie disparaisse définitivement.

## Faire dialoguer Orca, Linux et NVDA

Un autre projet est né d'un besoin beaucoup plus concret.

Dans mon travail, je dois régulièrement utiliser des machines Windows. Pourtant, mon environnement de prédilection reste Linux.

Une connexion Bureau à distance classique permet de récupérer le son de Windows, mais ce n'est pas toujours idéal pour un utilisateur de lecteur d'écran, notamment en matière de réactivité et de braille.

Je me suis donc intéressé au protocole **RDAccess**, utilisé pour transporter les informations d'accessibilité de NVDA à travers une connexion RDP.

J'ai développé, là encore avec l'aide de l'IA, un outil permettant de me connecter depuis Linux à une machine Windows exécutant NVDA, en exploitant ces canaux de communication.

L'objectif est de conserver une expérience d'accessibilité locale, avec la parole et le braille côté Linux, plutôt que de dépendre uniquement de la restitution audio distante.

Ce projet m'a demandé de comprendre des mécanismes de communication entre systèmes, des protocoles et des interfaces que je connaissais très mal.

Mais il répond à une idée qui me tient à cœur : **ce n'est pas à l'utilisateur de renoncer à son environnement préféré simplement parce qu'il a besoin d'une technologie d'assistance.**

Il y a trente ans, je devais choisir mes logiciels en fonction de ce que Sonolect était capable de lire.

Aujourd'hui, je cherche au contraire à faire communiquer les systèmes pour pouvoir utiliser les outils dont j'ai besoin, indépendamment de leur plateforme.

Le contraste est saisissant.

## Pourquoi Épaulard ?

Après avoir ressuscité Synthé3 et expérimenté les protocoles de communication des anciennes synthèses vocales, j'ai eu envie de retrouver un environnement informatique semblable à celui de mes débuts.

J'ai commencé par recréer l'émulation du protocole d'un synthétiseur vocal Apollo II, en utilisant Speech Dispatcher pour produire la parole sur un ordinateur moderne.

J'ai également travaillé sur l'émulation du protocole série utilisé par certaines plages braille Baum.

Puis une idée un peu déraisonnable s'est imposée : pourquoi ne pas développer un lecteur d'écran pour Windows 3.11 ?

C'est ainsi qu'est né **Épaulard**.

Écrit en C, il tente de rendre accessibles les applications Windows 3.11 en interceptant certaines opérations du système, en reconstituant les informations affichées et en les restituant par la parole et le braille.

Le travail est parfois déroutant.

Une calculatrice qui semble toute simple peut nécessiter un traitement particulier. Certains programmes dessinent leurs propres contrôles. D'autres utilisent des mécanismes qui échappent complètement à une analyse classique des fenêtres.

On découvre rapidement qu'il ne suffit pas de faire parler Windows.

Il faut essayer de comprendre ce qu'une personne voyante peut percevoir immédiatement, puis trouver comment transmettre cette information à quelqu'un qui ne voit pas.

Et recommencer, application après application.

Cette expérience m'a permis de mieux comprendre les difficultés auxquelles se heurtaient les développeurs de lecteurs d'écran il y a trente ans.

Mon admiration pour leur travail n'en est que plus grande.

Et quelque part, je retrouve aussi l'esprit des bénévoles du Club Micro-Son : des personnes qui développaient des solutions parce qu'elles avaient identifié un besoin auquel personne d'autre ne répondait vraiment.

## Un environnement entièrement reproductible

Mon objectif est désormais de publier Épaulard avec tout ce qui permet de reconstruire son environnement.

Grâce à Nix, il sera possible de préparer l'émulateur, d'appliquer les correctifs nécessaires, de compiler les composants et d'automatiser l'installation de MS-DOS 6.22, Windows 3.11 et Word 6.

Les utilisateurs devront fournir eux-mêmes leurs supports d'installation Microsoft. Aucun logiciel propriétaire ne sera redistribué avec le projet.

L'idée est de permettre à quelqu'un de retrouver un environnement Windows 3.11 fonctionnel, avec une synthèse vocale et une plage braille, sans devoir reconstituer manuellement toute la chaîne technique.

Un environnement que l'on ne se contente pas de regarder fonctionner.

**Un environnement que l'on peut réellement utiliser sans voir l'écran.**

## Pourquoi consacrer autant de temps à tout cela ?

C'est une question parfaitement légitime.

Après tout, aujourd'hui, GNOME et Orca me permettent de faire infiniment plus de choses que mon ancien PC sous DOS.

Et si je retrouvais demain les disquettes de Liste et de Lien, il est fort possible que je les utilise quelques minutes, simplement pour retrouver mes souvenirs, avant de retourner à mon environnement habituel.

Mais ce projet ne consiste pas à prétendre que l'informatique était meilleure autrefois.

Elle ne l'était pas.

L'accessibilité était coûteuse, souvent précaire et dépendait du travail d'un petit nombre de personnes passionnées.

Ce projet est une manière de préserver une partie de cette histoire, de comprendre les solutions imaginées à l'époque et de rendre hommage à ceux qui ont permis aux personnes non-voyantes d'accéder à l'informatique.

C'est aussi, plus personnellement, une manière de retrouver quelque chose que j'avais laissé derrière moi.

Je repense souvent à mes parents.

À l'argent qu'ils ont consacré à ces équipements hors de prix. À la confiance qu'ils m'ont accordée. À ce qu'ils espéraient probablement pour mon avenir en me donnant accès à un ordinateur.

Ils ne pouvaient pas savoir qu'un jour je travaillerais dans l'informatique, que je contribuerais à des logiciels libres utilisés par d'autres personnes, que je redonnerais vie à une synthèse vocale de mon enfance ou que je développerais des outils pour faire dialoguer Linux et Windows.

Et encore moins que je passerais mes soirées à recréer un lecteur d'écran pour un système vieux de trente ans.

Mais une chose est certaine : sans cet investissement initial, mon parcours aurait probablement été très différent.

Il y a quelque chose d'assez émouvant à constater qu'au début de cette histoire, je dépendais du travail de quelques passionnés pour pouvoir utiliser un ordinateur.

Et qu'aujourd'hui, grâce au logiciel libre et aux outils dont nous disposons, je peux à mon tour contribuer, même modestement, à rendre l'informatique plus accessible.

**Il y a trente ans, mes parents ont dépensé une fortune pour qu'un ordinateur puisse me parler.**

**Aujourd'hui, je lui redonne la parole, simplement pour ne pas oublier ce qu'ils m'ont offert.**
