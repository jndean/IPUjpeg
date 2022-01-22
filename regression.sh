
make && \
\
for img in imgs/*.jpg
do
    ./main $img
    if cmp -s outfile.* imgs/regression/$(basename $img); then
        echo -e "$img : \033[0;32mSUCCESS\033[0m"
    else
         echo -e "$img : \033[0;31mFAIL\033[0m"
    fi
    rm outfile.*
done