while read p; do
  git checkout api $p
done <sel4.files.changed
