#!/zsh

# Accept the target path as the first parameter, default to current directory if omitted
target_path="${1:-.}"

echo "Cleaning files in path: $target_path"

# Loop through all regular files recursively
for file in "$target_path"/**/*(.); do
  # 1. Removes actual hidden UTF-8 BOM characters (\xEF\xBB\xBF) or (\x{FEFF})
  # 2. Removes literal text occurrences of "<feff>" just in case it's typed out
  perl -pi -e 's/\x{FEFF}//g; s/\xEF\xBB\xBF//g; s/<feff>//g' "$file"
done

echo "Done!"
