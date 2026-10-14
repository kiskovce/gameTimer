#!/bin/sh
# Potrebuje: python3, python3-gi, gir1.2-gtk-3.0 (na Ubuntu desktop predinstalovane).
exec python3 "$(dirname "$0")/gametimer.py" "$@"
