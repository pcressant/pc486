# SPDX-License-Identifier: GPL-3.0-or-later
"""Services de l'hôte pour la VM avec --reseau : proxy web HTML 3.2 et courrier local.

La VM ne joint que ces services (QEMU restrict=on + guestfwd, 86Box dans un espace
de noms réseau vide) ; voir docs/reseau.md.
"""
