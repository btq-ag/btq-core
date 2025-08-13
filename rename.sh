#!/bin/bash

# IMPORTANT: Make sure to backup your repository before running these commands
# Example: cp -r btq-core btq-core-backup

# 1. First, let's find and rename all files with "btq" in their names
# We'll start with a dry run to see what would be renamed

echo "=== DRY RUN: Files that would be renamed ==="
find . -name "*btq*" -type f | while read file; do
    new_name=$(echo "$file" | sed 's/btq/btq/g' | sed 's/BTQ/BTQ/g')
    echo "Would rename: $file -> $new_name"
done

echo -e "\n=== DRY RUN: Directories that would be renamed ==="
find . -name "*btq*" -type d | while read dir; do
    new_name=$(echo "$dir" | sed 's/btq/btq/g' | sed 's/BTQ/BTQ/g')
    echo "Would rename: $dir -> $new_name"
done

# 2. Rename files (bottom-up to avoid path issues)
echo -e "\n=== Renaming files with 'btq' in their names ==="
find . -name "*btq*" -type f | sort -r | while read file; do
    dir=$(dirname "$file")
    basename=$(basename "$file")
    new_basename=$(echo "$basename" | sed 's/btq/btq/g' | sed 's/BTQ/BTQ/g')
    if [ "$basename" != "$new_basename" ]; then
        echo "Renaming: $file -> $dir/$new_basename"
        mv "$file" "$dir/$new_basename"
    fi
done

# 3. Rename directories (bottom-up to avoid path issues)
echo -e "\n=== Renaming directories with 'btq' in their names ==="
find . -name "*btq*" -type d | sort -r | while read dir; do
    parent=$(dirname "$dir")
    basename=$(basename "$dir")
    new_basename=$(echo "$basename" | sed 's/btq/btq/g' | sed 's/BTQ/BTQ/g')
    if [ "$basename" != "$new_basename" ]; then
        echo "Renaming: $dir -> $parent/$new_basename"
        mv "$dir" "$parent/$new_basename"
    fi
done

# 4. Replace content in all text files
echo -e "\n=== Replacing content in files ==="

# Find all text files (excluding binary files, git files, and build artifacts)
find . -type f \
    ! -path "./.git/*" \
    ! -path "./build/*" \
    ! -path "./autom4te.cache/*" \
    ! -name "*.o" \
    ! -name "*.so" \
    ! -name "*.a" \
    ! -name "*.dylib" \
    ! -name "*.dll" \
    ! -name "*.exe" \
    ! -name "*.ico" \
    ! -name "*.png" \
    ! -name "*.jpg" \
    ! -name "*.jpeg" \
    ! -name "*.gif" \
    ! -name "*.bmp" \
    ! -name "*.svg" \
    ! -name "*.pdf" \
    ! -name "*.bin" \
    ! -name "*.dat" \
    ! -name "*.db" \
    ! -name "*.lock" \
    -exec file {} \; | grep -E "(text|script)" | cut -d: -f1 | while read file; do
    
    # Check if file contains any of our target strings
    if grep -q -E "(BTQ|btq|BTQ)" "$file" 2>/dev/null; then
        echo "Processing: $file"
        
        # Create a backup
        cp "$file" "$file.bak"
        
        # Perform replacements - order matters to avoid partial replacements
        sed -i \
            -e 's/\bBTQ\b/BTQ/g' \
            -e 's/\bbtq\b/btq/g' \
            -e 's/\bBTC\b/BTQ/g' \
            -e 's/BTQ/BTQ/g' \
            -e 's/BTQ/BTQ/g' \
            -e 's/btq/btq/g' \
            "$file"
        
        # Check if the file actually changed
        if ! cmp -s "$file" "$file.bak"; then
            echo "  Updated: $file"
        else
            echo "  No changes: $file"
        fi
        
        # Remove backup
        rm "$file.bak"
    fi
done

# 5. Special handling for specific file types that might need careful attention

echo -e "\n=== Special handling for specific files ==="

# Handle Makefiles and build scripts with more careful replacements
find . -name "Makefile*" -o -name "*.mk" -o -name "*.am" -o -name "*.ac" -o -name "configure*" | while read file; do
    if [ -f "$file" ] && grep -q -E "(BTQ|btq|BTQ)" "$file" 2>/dev/null; then
        echo "Build file: $file"
        cp "$file" "$file.bak"
        
        # More conservative replacements for build files
        sed -i \
            -e 's/btq-core/btq-core/g' \
            -e 's/btq_/btq_/g' \
            -e 's/libbtq/libbtq/g' \
            -e 's/BTQ/BTQ/g' \
            -e 's/BTQ/BTQ/g' \
            -e 's/btq/btq/g' \
            "$file"
        
        if ! cmp -s "$file" "$file.bak"; then
            echo "  Updated build file: $file"
        fi
        rm "$file.bak"
    fi
done

# 6. Handle Qt resource files and UI files
find . -name "*.qrc" -o -name "*.ui" -o -name "*.ts" -o -name "*.xlf" | while read file; do
    if [ -f "$file" ] && grep -q -E "(BTQ|btq|BTQ)" "$file" 2>/dev/null; then
        echo "Qt file: $file"
        cp "$file" "$file.bak"
        
        sed -i \
            -e 's/BTQ/BTQ/g' \
            -e 's/btq/btq/g' \
            -e 's/BTQ/BTQ/g' \
            "$file"
        
        if ! cmp -s "$file" "$file.bak"; then
            echo "  Updated Qt file: $file"
        fi
        rm "$file.bak"
    fi
done

echo -e "\n=== Renaming complete! ==="
echo "Please review the changes and test the build before committing."
echo "You may need to regenerate some auto-generated files like configure scripts."

# 7. Summary of what was changed
echo -e "\n=== Summary ==="
echo "Files renamed:"
find . -name "*btq*" -type f | head -20
echo -e "\nDirectories renamed:"
find . -name "*btq*" -type d | head -10