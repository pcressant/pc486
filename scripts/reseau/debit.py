# SPDX-License-Identifier: GPL-3.0-or-later
"""Débit limité des services vers la VM (proxy, POP3), commun à toutes les connexions.

Le réseau virtuel de QEMU comme celui de 86Box livre les données aussi vite que
l'invité les prend : un téléchargement occupe alors tout le 486 émulé (interruptions
de la NE2000, TCP/IP-32) et Windows ne lit plus le clavier, Épaulard compris
(mesure du 30 octobre 2026, tests/test_reseau_vm.py). Un seau à jetons partagé borne
le débit total envoyé à la VM ; PC486_DEBIT en Ko/s (0 : sans limite).
"""
import os
import threading
import time

DEFAULT = 32
CHUNK = 1460


def rate_from_env():
    try:
        value = int(os.environ.get('PC486_DEBIT', DEFAULT))
    except ValueError:
        value = DEFAULT
    return max(0, value) * 1024


class Bucket:
    def __init__(self, rate):
        self.rate = rate
        self.burst = max(CHUNK * 2, rate // 10)
        self.tokens = self.burst
        self.stamp = time.monotonic()
        self.lock = threading.Lock()

    def take(self, n):
        """Attend que n octets puissent partir (n ≤ burst)."""
        if not self.rate:
            return
        while True:
            with self.lock:
                now = time.monotonic()
                self.tokens = min(self.burst, self.tokens + (now - self.stamp) * self.rate)
                self.stamp = now
                if self.tokens >= n:
                    self.tokens -= n
                    return
                wait = (n - self.tokens) / self.rate
            time.sleep(wait)


BUCKET = Bucket(rate_from_env())


class Limited:
    """Fichier d'écriture d'une connexion, découpé et cadencé par le seau commun."""

    def __init__(self, raw, bucket=None):
        self.raw, self.bucket = raw, bucket or BUCKET

    def write(self, data):
        view = memoryview(data)
        for i in range(0, len(view), CHUNK):
            part = view[i:i + CHUNK]
            self.bucket.take(len(part))
            self.raw.write(part)
        return len(data)

    def flush(self):
        self.raw.flush()

    def __getattr__(self, name):
        return getattr(self.raw, name)
