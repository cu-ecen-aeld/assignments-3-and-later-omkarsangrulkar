#!/bin/bash
# Quick cleanup script for your repository
# This is the SAFE option - removes build artifacts and resets submodules

set -e

echo "=== Repository Cleanup Script ==="
echo "This will:"
echo "  1. Remove build directories"
echo "  2. Remove auto-generated test runners"
echo "  3. Remove compiled binaries"
echo "  4. Reset the assignment-autotest submodule"
echo ""
read -p "Continue? (y/n) " -n 1 -r
echo
if [[ ! $REPLY =~ ^[Yy]$ ]]; then
    echo "Cleanup cancelled."
    exit 0
fi

echo ""
echo "Step 1: Cleaning build artifacts..."
rm -rf build/
rm -rf assignment-autotest/build/ 2>/dev/null || true
find . -name "*_Runner.c" -delete
find . -name "*.o" -delete
find . -name "*.a" -delete
find . -name "*.so" -delete

echo ""
echo "Step 2: Removing compiled writer binary if exists..."
if [ -f "writer" ]; then
    rm -f writer
fi

echo ""
echo "Step 3: Resetting assignment-autotest submodule..."
git submodule deinit -f assignment-autotest
rm -rf .git/modules/assignment-autotest
git rm -f assignment-autotest
rm -rf assignment-autotest

echo ""
echo "Step 4: Re-adding assignment-autotest submodule..."
git submodule add https://github.com/cu-ecen-aeld/assignment-autotest.git assignment-autotest
git submodule update --init assignment-autotest

echo ""
echo "Step 5: Creating/updating .gitignore..."
cat > .gitignore << 'EOF'
# Build artifacts
build/
*_Runner.c
*.o
*.a
*.so
writer

# Temporary files
/tmp/
*.tmp
*.log

# Editor files
.vscode/
.idea/
*.swp
*~
.DS_Store
EOF

echo ""
echo "Step 6: Staging all changes..."
git add -A

echo ""
echo "=== Cleanup Complete ==="
echo ""
echo "Changes staged. Review with: git status"
echo "Commit with: git commit -m 'Clean repository and reset submodules'"
echo "Push with: git push"
