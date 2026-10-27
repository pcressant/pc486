# Licences et provenance

Le code et la documentation originaux du projet **pc486 / Épaulard / Épaudos** sont
redistribuables et modifiables selon la **GNU General Public License, version 3**,
ou, à votre choix, toute version ultérieure : **GPL-3.0-or-later**.
Le texte de référence est dans [LICENSE](../LICENSE).
Le projet est fourni sans garantie, dans les conditions précisées par cette licence.

Les éléments tiers conservent leurs licences et leurs attributions :

| Élément | Provenance et licence |
| --- | --- |
| `epaulard/src/brfr.h` | Données des tables françaises de liblouis 3.33.0, **LGPL-2.1-or-later**. Les auteurs et sources restent indiqués dans l'en-tête. Texte : [LGPL 2.1](LGPL-2.1-or-later.txt). |
| `epaudos/build/br850.h` (généré) | Même table liblouis que `brfr.h`, réindexée en CP850 à la compilation ; mêmes attributions et licence **LGPL-2.1-or-later**. |
| `tools/86box-pc486.patch` | Correctif dérivé du code de 86Box, **GPL-2.0-or-later**, conformément à la licence amont. Les mentions des auteurs du code de contexte sont conservées. Texte : [GPL 2](GPL-2.0-or-later.txt). |

Sources des notices :

- [Tables de liblouis 3.33.0](https://github.com/liblouis/liblouis/tree/v3.33.0/tables).
- [Licence annoncée par 86Box](https://github.com/86Box/86Box#license).
- Textes des licences : [GPL 3](https://github.com/spdx/license-list-data/blob/main/text/GPL-3.0-or-later.txt),
  [GPL 2](https://github.com/spdx/license-list-data/blob/main/text/GPL-2.0-or-later.txt)
  et [LGPL 2.1](https://github.com/spdx/license-list-data/blob/main/text/LGPL-2.1-or-later.txt),
  reproduits depuis les données SPDX.

Les systèmes et applications Microsoft, les ROM et les dépendances
installées séparément restent régis par leurs propres licences. Les supports et
les images disque générées par l'installateur restent locaux ; la GPL du projet
ne leur est pas appliquée.
