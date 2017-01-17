#! /bin/sh

# adapted from https://gist.github.com/vidavidorra/548ffbcdae99d752da02

set -e

git clone -b gh-pages "https://git@$GH_REPO_REF"
cd "$GH_REPO_NAME"

# Pretend to be a user called Travis CI.
git config --global push.default simple
git config user.name "Travis CI"
git config user.email "travis@travis-ci.org"

# Remove everything currently in the gh-pages branch. GitHub is smart enough to
# know which files have changed and which files have stayed the same and will
# only update the changed files. So the gh-pages branch can be safely cleaned,
# and it is sure that everything pushed later is the new documentation.
rm -rf ./*

# Need to create a .nojekyll file to allow filenames starting with an underscore
# to be seen on the gh-pages site. Therefore creating an empty .nojekyll file.
# Presumably this is only needed when the SHORT_NAMES option in Doxygen is set
# to NO, which it is by default. So creating the file just in case.
touch .nojekyll

# Generate the Doxygen code documentation and log the output.
doxygen "$DOXYFILE" 2>&1 | tee doxygen.log

# Upload the documentation to the gh-pages branch of the repository. Only upload
# if Doxygen successfully created the documentation. Check this by verifying
# that the html directory and the file html/index.html both exist. This is a
# good indication that Doxygen did it's work.
if [ -d "html" ] && [ -f "html/index.html" ]; then
    # Add everything in this directory (the Doxygen code documentation) to the
    # gh-pages branch. GitHub is smart enough to know which files have changed
    # and which files have stayed the same and will only update the changed
    # files.
    git add --all

    # Commit the added files with a title and description containing the Travis
    # CI build number and the GitHub commit reference that issued this build.
    git commit -m "Update doxygen documentation on branch gh-pages." \
        -m "Travis CI build ${TRAVIS_BUILD_NUMBER}." \
        -m "Commit: ${TRAVIS_COMMIT}."

    # Force push to the remote gh-pages branch. The ouput is redirected to
    # /dev/null to hide any sensitive credential data that might otherwise be
    # exposed.
    git push --force "https://${GH_REPO_TOKEN}@${GH_REPO_REF}" > /dev/null 2>&1
fi
