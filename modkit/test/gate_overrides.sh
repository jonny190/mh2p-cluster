#!/usr/bin/env bash
set -u
SCR="$1"; REPO="$2"; export PATH="$SCR/stubs:$PATH"
SYMS_SO=$SCR/zip34/Mods/ClusterIntegration/Update/gal_cluster.so
setup() { # $1 release, $2 with_sportchrono, $3 with_aa_jar
  rm -rf /mnt/app /fs; mkdir -p /mnt/app/armle/usr/bin /mnt/app/eso/hmi/lsd/jars /mnt/app/eso/bin/apps /mnt/app/eso/lib
  printf 'GALBIN\n' > /mnt/app/eso/bin/apps/gal; printf 'DIOBIN\n' > /mnt/app/eso/bin/apps/dio_manager; chmod 755 /mnt/app/eso/bin/apps/*
  printf '#!/bin/sh\nprintf "%%060d%%s\\n" 0 "'"$1"'"\n' > /mnt/app/armle/usr/bin/pc; chmod 755 /mnt/app/armle/usr/bin/pc
  rm -rf $SCR/fakejars && mkdir -p $SCR/fakejars/de/audi/app/terminalmode/smartphone/androidauto2 $SCR/fakejars/de/audi/app/car/adi/legacy/sportchrono
  echo x > $SCR/fakejars/de/audi/app/terminalmode/smartphone/androidauto2/AndroidAuto2Subsystem.class
  (cd $SCR/fakejars && zip -q /mnt/app/eso/hmi/lsd/jars/de.audi.app.terminalmode.jar de/audi/app/terminalmode/smartphone/androidauto2/AndroidAuto2Subsystem.class)
  if [[ "$2" == 1 ]]; then echo y > $SCR/fakejars/de/audi/app/car/adi/legacy/sportchrono/StorageMountHandler.class; (cd $SCR/fakejars && zip -q /mnt/app/eso/hmi/lsd/jars/de.audi.app.car.jar de/audi/app/car/adi/legacy/sportchrono/StorageMountHandler.class); fi
  { head -c 200 /dev/urandom; readelf --dyn-syms -W $SYMS_SO | awk '$7!="UND" && $4=="FUNC" && $5=="GLOBAL" {print $8}' | grep '^_ZN' | grep -v sendChannelOpenResp; head -c 200 /dev/urandom; } > /mnt/app/eso/lib/libautoreceiver.so
  rm -rf "$SCR/fakesd"; mkdir -p "$SCR/fakesd/Mods/ClusterIntegration"; cp -r "$REPO/modkit/Mods/ClusterIntegration/Update" "$SCR/fakesd/Mods/ClusterIntegration/"
  for f in ClusterIntegration_vTEST.jar cluster gal_cluster.so dio_cluster.so; do printf 'payload %s\n' "$f" > "$SCR/fakesd/Mods/ClusterIntegration/Update/$f"; done
  rm -rf "$SCR/fakesd/Mods/ClusterIntegration/Update/aa_only"
  if [[ "$3" == 1 ]]; then mkdir -p "$SCR/fakesd/Mods/ClusterIntegration/Update/aa_only"; printf 'aa\n' > "$SCR/fakesd/Mods/ClusterIntegration/Update/aa_only/ClusterIntegration_vTEST_aa.jar"; printf 'aa cluster\n' > "$SCR/fakesd/Mods/ClusterIntegration/Update/aa_only/cluster"; fi
  cp $SCR/zip34/Mods/ClusterIntegration/Update/cluster_config.json "$SCR/fakesd/Mods/ClusterIntegration/Update/"
  export MOD_PATH="$SCR/fakesd/Mods/ClusterIntegration/Update"
}
CFG=/mnt/app/eso/bin/apps/cluster/cluster_config.json
res() { printf '%-44s %s\n' "$1" "$2"; }
echo "=== overrides: CRLF file, trailing comment, nested key, out-of-block keys, bad value ==="
setup MH2p_ER_AUG35_P2873 1 1
printf 'forceRHD=true  # RHD car\r\nimperialSmallUnit="yards"\r\nzoomX=2.0\r\ndistance=42\r\nimperial=false\r\nname="e-tron"\r\nheartbeatInterval=abc\r\nlogFileSize=25\r\n# comment line\r\n' > "$SCR/fakesd/Mods/ClusterIntegration/config_overrides.txt"
out=$(mksh "$MOD_PATH/install.sh" 2>&1); echo "$out" | grep -E "config override|hook symbols" | sed 's/^/    | /'
python3 - "$CFG" "$SCR/zip34/Mods/ClusterIntegration/Update/cluster_config.json" <<'PY'
import json,sys
new=json.load(open(sys.argv[1])); old=json.load(open(sys.argv[2],encoding='utf-8'))
def res(n,c): print(("PASS " if c else "FAIL ")+n)
res("valid JSON after overrides", True)
res("forceRHD true (comment stripped)", new["config"]["forceRHD"] is True)
res("imperialSmallUnit yards (CRLF stripped)", new["config"]["imperialSmallUnit"]=="yards")
res("logFileSize 25", new["config"]["logFileSize"]==25)
res("heartbeatInterval unchanged (bad value)", new["config"]["heartbeatInterval"]==old["config"]["heartbeatInterval"])
g=dict(old["config"]["gal_h264"]); g.update({"codecRes":1080,"mode":"fill","panY":0.0})
res("nested mirror untouched, gal_h264 = shipped + Audi defaults (1080, fill, panY 0)", new["config"]["mirror"]==old["config"]["mirror"] and new["config"]["gal_h264"]==g)
res("dynamicThresholds untouched", new["dynamicThresholds"]==old["dynamicThresholds"])
res("countries untouched", new["countries"]==old["countries"])
res("carConfig untouched", new["carConfig"]==old["carConfig"])
res("no CR left in file", b"\r" not in open(sys.argv[1],'rb').read())
PY
grep -c "override" "$MOD_PATH/Backup/manifest.txt" | sed 's/^/    | manifest override lines: /'
echo "=== root cluster_config.json warning ==="; mkdir -p /fs/sda0; cp $CFG /fs/sda0/cluster_config.json; out=$(mksh "$MOD_PATH/install.sh" 2>&1); echo "$out" | grep -q "WARN: /fs/sda0/cluster_config.json exists" && res "root config warning" PASS || res "root config warning" FAIL; rm -rf /fs
echo "=== diag + overrides via apply_override ==="; touch "$SCR/fakesd/Mods/ClusterIntegration/diag.txt"; mksh "$MOD_PATH/install.sh" >/dev/null 2>&1
python3 -c "import json;c=json.load(open('$CFG'))['config'];print(('PASS' if c['enableMapRender'] is False and c['forceRHD'] is True else 'FAIL')+' diag: enableMapRender=false, forceRHD kept')"
rm -f "$SCR/fakesd/Mods/ClusterIntegration/diag.txt"
echo "=== manifest-driven cleanup of an extra 'add' ==="; echo "extra" > /mnt/app/eso/bin/apps/cluster/extra_future_file; echo "20260101_000000 add /mnt/app/eso/bin/apps/cluster/extra_future_file" >> "$MOD_PATH/Backup/manifest.txt"; echo "20260101_000000 add /mnt/app/eso/bin/apps/gal" >> "$MOD_PATH/Backup/manifest.txt"
out=$(mksh "$MOD_PATH/uninstall.sh" 2>&1); [[ ! -f /mnt/app/eso/bin/apps/cluster/extra_future_file && ! -d /mnt/app/eso/bin/apps/cluster && "$(cat /mnt/app/eso/bin/apps/gal)" == "GALBIN" ]] && res "manifest add cleanup (own dirs only)" PASS || res "manifest add cleanup (own dirs only)" FAIL
echo "=== aa_only jar missing on Audi -> abort before change ==="; setup MH2p_ER_AUG35_P2873 1 0; out=$(mksh "$MOD_PATH/install.sh" 2>&1); rc=$?; echo "$out" | grep -E "ERROR" | sed 's/^/    | /'; [[ $rc == 4 && ! -f /mnt/app/eso/bin/apps/gal.real && -z "$(ls /mnt/app/eso/hmi/lsd/jars | grep ClusterIntegration)" ]] && res "abort without aa_only jar" PASS || res "abort without aa_only jar" FAIL
touch "$SCR/fakesd/Mods/ClusterIntegration/enable_carplay.txt"; out=$(mksh "$MOD_PATH/install.sh" 2>&1); [[ -f /mnt/app/eso/hmi/lsd/jars/ClusterIntegration_vTEST.jar ]] && res "enable_carplay.txt allows full jar" PASS || res "enable_carplay.txt allows full jar" FAIL
echo "=== wrappers: no SD-root marker any more ==="; mkdir -p /fs/sda0; touch /fs/sda0/cluster_diag.txt; printf '#!/bin/sh\necho "MERGE=$GAL_CLUSTER_MERGE"\n' > /mnt/app/eso/bin/apps/gal.real; chmod 755 /mnt/app/eso/bin/apps/gal.real; mksh /mnt/app/eso/bin/apps/gal 2>/dev/null | grep -q "MERGE=$" && res "root marker ignored" PASS || res "root marker ignored" FAIL; rm -rf /fs
echo "=== 27xx Audi warning ==="; setup MH2p_ER_AUG35_P2711 1 1; out=$(mksh "$MOD_PATH/install.sh" 2>&1); echo "$out" | grep -q "outside the 28xx window" && res "27xx warning printed" PASS || res "27xx warning printed" FAIL
echo "=== P2711 with altscreen: no Porsche-window warning ==="; setup MH2p_US_AUG35_P2711 1 1; touch "$SCR/fakesd/Mods/ClusterIntegration/enable_carplay_altscreen.txt"; out=$(mksh "$MOD_PATH/install.sh" 2>&1); echo "$out" | grep -q "made and tested on" && ! echo "$out" | grep -q -e "outside the 28xx window" -e "EXPERIMENTAL" && echo "$out" | grep -q "re-run the ModKit" && res "P2711 altscreen: toned-down message" PASS || res "P2711 altscreen: toned-down message" FAIL
echo "=== Audi JAR without its daemon (aa_only/cluster) -> abort before change ==="
setup MH2p_ER_AUG35_P2711 1 1; rm -f "$MOD_PATH/aa_only/cluster"; out=$(mksh "$MOD_PATH/install.sh" 2>&1); rc=$?
[[ $rc == 4 && ! -f /mnt/app/eso/bin/apps/gal.real ]] && echo "$out" | grep -q 'aa_only/cluster not found' && res "abort without aa_only/cluster" PASS || res "abort without aa_only/cluster" "FAIL (rc=$rc)"
echo "=== altscreen: enable_carplay_altscreen.txt ==="
AC=/mnt/app/eso/bin/apps/cluster
setup MH2p_ER_AUG35_P2711 1 1; touch "$SCR/fakesd/Mods/ClusterIntegration/enable_carplay_altscreen.txt"
out=$(mksh "$MOD_PATH/install.sh" 2>&1); rc=$?; echo "$out" | grep -E "carplay|cluster daemon|ERROR" | sed 's/^/    | /'
[[ $rc == 0 && -f $AC/altscreen.so && -x $AC/altscreen_pf.sh && -f /mnt/app/eso/bin/apps/dio_manager.real ]] && res "altscreen: hook, pf script and dio_manager wrapper installed" PASS || res "altscreen: files installed" "FAIL (rc=$rc)"
[[ "$(cat $AC/cluster)" == "aa cluster" ]] && res "altscreen: daemon from aa_only (displayable 62)" PASS || res "altscreen: daemon from aa_only" FAIL
python3 - "$CFG" "$SCR/zip34/Mods/ClusterIntegration/Update/cluster_config.json" <<'PY'
import json,sys
new=json.load(open(sys.argv[1])); old=json.load(open(sys.argv[2],encoding='utf-8'))
def res(n,c): print(("PASS " if c else "FAIL ")+n)
m=dict(old["config"]["mirror"]); m["mode"]="fill capture=h264"
res("altscreen: config mirror.mode = fill capture=h264, rest of mirror kept", new["config"]["mirror"]==m)
g=dict(old["config"]["gal_h264"]); g.update({"codecRes":1080,"mode":"fill","panY":0.0})
res("altscreen: gal_h264 = shipped + Audi defaults, carConfig untouched", new["config"]["gal_h264"]==g and new["carConfig"]==old["carConfig"])
PY
printf '#!/bin/sh\necho "PRELOAD=$LD_PRELOAD"\n' > /mnt/app/eso/bin/apps/dio_manager.real; chmod 755 /mnt/app/eso/bin/apps/dio_manager.real
mksh /mnt/app/eso/bin/apps/dio_manager 2>/dev/null | grep -q "PRELOAD=$AC/altscreen.so$" && res "altscreen: wrapper preloads altscreen.so" PASS || res "altscreen: wrapper preloads altscreen.so" FAIL
out=$(mksh "$MOD_PATH/install.sh" 2>&1); echo "$out" | grep -q "skip identical:    $AC/altscreen.so" && echo "$out" | grep -q 'carplay altscreen: ON' && res "altscreen: re-run idempotent" PASS || res "altscreen: re-run idempotent" FAIL
touch "$SCR/fakesd/Mods/ClusterIntegration/enable_carplay.txt"; out=$(mksh "$MOD_PATH/install.sh" 2>&1)
echo "$out" | grep -q 'enable_carplay_altscreen.txt needs the Audi JAR' && [[ ! -f $AC/altscreen.so ]] && res "altscreen: ignored and removed with enable_carplay.txt" PASS || res "altscreen: ignored with enable_carplay.txt" FAIL
rm -f "$SCR/fakesd/Mods/ClusterIntegration/enable_carplay.txt"; mksh "$MOD_PATH/install.sh" >/dev/null 2>&1
rm -f "$SCR/fakesd/Mods/ClusterIntegration/enable_carplay_altscreen.txt"; out=$(mksh "$MOD_PATH/install.sh" 2>&1)
python3 -c "import json;c=json.load(open('$CFG'))['config'];print(('PASS' if c['mirror']['mode']=='fill' else 'FAIL')+' altscreen off: shipped mirror.mode back')"
[[ ! -f $AC/altscreen.so && ! -f $AC/altscreen_pf.sh && ! -f /mnt/app/eso/bin/apps/dio_manager.real ]] && echo "$out" | grep -q 'remove altscreen:' && res "altscreen off: files removed, stock dio_manager back" PASS || res "altscreen off: files removed" FAIL
touch "$SCR/fakesd/Mods/ClusterIntegration/enable_carplay_altscreen.txt"; mksh "$MOD_PATH/install.sh" >/dev/null 2>&1; echo "phys=384,144" > $AC/altscreen.conf
out=$(mksh "$MOD_PATH/uninstall.sh" 2>&1); [[ ! -d $AC && "$(head -1 /mnt/app/eso/bin/apps/dio_manager)" != "#!/bin/ksh" ]] && ls "$MOD_PATH/Backup" | grep -q '^altscreen.conf.removed' && res "altscreen: uninstall removes everything, keeps altscreen.conf in Backup" PASS || res "altscreen: uninstall" FAIL
rm -rf /mnt/app /fs /tmp/gal_preload.log /tmp/dio_preload.log
