#!/bin/bash

# File to store progress
PROGRESS_FILE=".cherry_pick_progress"

# Get the list of commits from 0v branch that aren't in 4.4-experimental
if [ ! -f "$PROGRESS_FILE" ]; then
    git log 4.4-experimental..0v --pretty=format:"%H" | tac > "$PROGRESS_FILE"
fi

# Function to remove a commit from the progress file
remove_from_progress()
{
    sed -i.bak "/^$1$/d" "$PROGRESS_FILE" && rm "$PROGRESS_FILE.bak"
}

# Cherry-pick each commit
while read -r commit; do
    # Check if the commit is already applied
    if git cherry -v HEAD 4.4-experimental | grep -q "$commit"; then
        echo "Commit $commit already applied, skipping..."
        remove_from_progress "$commit"
        continue
    fi

    # Check if the commit message contains "pck-signing"
    if ! git show -s --format=%B $commit | grep -iq "signing"; then
        if git cherry-pick -x $commit; then
            echo "Successfully cherry-picked commit $commit"
            remove_from_progress "$commit"
        else
            echo "Conflict in cherry-picking commit $commit"
            echo "Resolve the conflict and then run:"
            echo "git cherry-pick --continue"
            echo "Then run this script again to resume from this point"
            exit 1
        fi
    else
        echo "Skipping pck-signing related commit: $commit"
        remove_from_progress "$commit"
    fi
done < "$PROGRESS_FILE"

echo "Cherry-picking complete!"
rm -f "$PROGRESS_FILE"
