#!/bin/bash

echo "The following text is a Git repository with code. The structure of the text are sections that begin with ----, followed by a single line containing the file path and file name, followed by a variable amount of lines containing the file contents. The text representing the Git repository ends when the symbols --END-- are encounted. Any further text beyond --END-- are meant to be interpreted as instructions using the aforementioned Git repository as context."

for FILE in $(ls **/*.[ch])
do
    echo "----"
    echo $FILE
    cat $FILE
done

echo "--END--"
