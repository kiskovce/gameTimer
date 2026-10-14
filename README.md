# Game Timer

Opakujúci sa časovač s GUI. Po uplynutí periódy (hh:mm:ss) sa okno zobrazí navrchu pred ostatnými oknami, vydá zvukový alarm a bliká po nastavený čas.

Testované na Ubuntu 22.04. Binárka zostavená na 22.04 pobeží aj na 24.04 a 26.04.

## Požiadavky na zostavenie

```bash
sudo apt install build-essential pkg-config libgtk-3-dev
```

Za behu stačí desktopová inštalácia Ubuntu (GTK3 a `paplay` alebo `aplay` pre zvuk).

## Zostavenie

```bash
git clone git@github.com:kiskovce/gameTimer.git
cd gameTimer
make
./gametimer
```

## Inštalácia do menu (pre aktuálneho používateľa)

```bash
mkdir -p ~/.local/bin ~/.local/share/applications
cp gametimer ~/.local/bin/

cat > ~/.local/share/applications/gametimer.desktop <<EOF
[Desktop Entry]
Type=Application
Name=Game Timer
Comment=Opakujúci sa časovač s alarmom
Exec=$HOME/.local/bin/gametimer
Icon=alarm-symbolic
Terminal=false
Categories=Utility;
EOF

update-desktop-database ~/.local/share/applications
```

Aplikáciu potom nájdeš v prehľade aplikácií (kláves Super). Ak sa nezobrazí, odhlás sa a znova prihlás.

## Odinštalovanie

```bash
rm ~/.local/bin/gametimer ~/.local/share/applications/gametimer.desktop
rm -r ~/.config/gametimer
```

## Poznámky

- Nastavenia sa ukladajú do `~/.config/gametimer/settings.ini`.
- Pod Waylandom GNOME bráni oknám brať si focus. Aplikácia preto vynúti XWayland (`GDK_BACKEND=x11`), kde okno vyskočí navrch.
- Zostavuj na najstaršej podporovanej verzii (22.04), aby binárka fungovala aj na novších.
