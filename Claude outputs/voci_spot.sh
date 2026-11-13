#!/bin/bash
# Voci dello spot "Ciao AXA" (30 s) con una voce maschile del Mac.
# Uso:   bash voci_spot.sh
#        bash voci_spot.sh "Luca (Premium)"            (stessa voce per tutti)
#        bash voci_spot.sh "Luca (Premium)" "Luca"      (voce di Marco, voce del robot)
#
# Voce Premium: Impostazioni di Sistema > Accessibilità > Contenuti letti ad alta voce >
#   Voce di sistema > Gestisci voci… > Italiano > Luca (Premium)  (scaricala una volta)
# Voci italiane installate:   say -v '?' | grep it_IT

# sceglie la voce maschile migliore installata
miglior_luca() {
  for v in "Luca (Premium)" "Luca (Enhanced)" "Luca (Migliorata)" "Luca"; do
    say -v '?' | grep -q "^$v " && { echo "$v"; return; }
  done
  echo "Luca"
}
VOCE_MARCO="${1:-$(miglior_luca)}"
VOCE_ROBOT="${2:-$VOCE_MARCO}"
VEL_MARCO=165        # Marco: ritmo normale
VEL_ROBOT=180        # robot: un po' più svelto e brillante
CARTELLA="voci_spot"
mkdir -p "$CARTELLA"
P="[[slnc 250]]"     # piccola pausa

echo "Voce di Marco: $VOCE_MARCO   Voce del robot: $VOCE_ROBOT"

# nome file | chi | testo | secondi disponibili nella scena
battute=(
"01_marco_dubbi|M|Franchigia? $P Massimale? $P Ma che vuol dire?|4.5"
"02_marco_ciao|M|Ciao Axa!|1.8"
"03_robot_eccomi|R|Eccomi!|1.5"
"04_robot_franchigia|R|La franchigia $P è la parte del danno che resta a te.|4.5"
"05_robot_esempio|R|Danno da mille euro, $P franchigia duecento: $P ottocento li paga l'assicurazione.|4.8"
"06_marco_chiaro|M|Chiarissimo!|1.6"
"07_robot_non_vendo|R|Io spiego, $P non vendo.|2.4"
"08_chiusura|R|Assistente vocale Axa. $P Le assicurazioni, $P in parole semplici.|4.5"
)

for riga in "${battute[@]}"; do
  IFS='|' read -r nome chi testo limite <<< "$riga"
  if [ "$chi" = "M" ]; then voce="$VOCE_MARCO"; vel=$VEL_MARCO; else voce="$VOCE_ROBOT"; vel=$VEL_ROBOT; fi
  file="$CARTELLA/$nome.aiff"
  say -v "$voce" -r "$vel" -o "$file" "$testo" || { echo "Voce \"$voce\" non trovata: controlla con  say -v '?' | grep it_IT"; exit 1; }
  sec=$(afinfo "$file" | awk '/estimated duration/ {printf "%.1f", $3}')
  stato="ok"
  awk "BEGIN{exit !($sec > $limite)}" && stato="LUNGA (max $limite s): alza la velocità"
  echo "$nome: $sec s  $stato"
done

echo "Fatto: i file sono nella cartella $CARTELLA"
open "$CARTELLA"
