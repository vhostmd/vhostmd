echo "Running libtoolize ..." &&
libtoolize --copy --force

echo "Running aclocal ..." &&
aclocal --force &&


echo "Running autoheader ..." &&
autoheader --force &&

echo "Running automake ..." &&
automake -i --add-missing --copy --foreign &&

echo "Running autoconf ..." &&
autoconf --force &&

echo "You may now run ./configure"
