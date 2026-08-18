#!/bin/sh
# Turn demo/index.html into a C string constant (hush_ui_html.h).
set -eu
if [ "$#" -lt 1 ]; then
    echo "usage: embed-ui.sh FILE" >&2
    exit 1
fi
src="$1"
printf '%s\n' '/* generated from demo/index.html — do not edit */'
printf '%s\n' '#ifndef HUSH_UI_HTML_H'
printf '%s\n' '#define HUSH_UI_HTML_H'
printf '%s\n' 'static const char HUSH_UI_HTML[] ='
sed 's/\\/\\\\/g; s/"/\\"/g' "$src" | while IFS= read -r line || [ -n "$line" ]; do
    printf '"%s\\n"\n' "$line"
done
printf '%s\n' ';'
printf '%s\n' '#endif /* HUSH_UI_HTML_H */'
