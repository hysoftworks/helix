source common.sh

clearStore
clearCache

nix shell -f shell-hello.nix hello -c hello | grep 'Hello World'
nix shell -f shell-hello.nix hello -c hello NixOS | grep 'Hello NixOS'

# Test output selection.
nix shell -f shell-hello.nix hello^dev -c hello2 | grep 'Hello2'
nix shell -f shell-hello.nix 'hello^*' -c hello2 | grep 'Hello2'


if isDaemonNewer "2.20.0pre20231220"; then
    # Test that command line attribute ordering is reflected in the PATH
    # https://github.com/NixOS/nix/issues/7905
    nix shell -f shell-hello.nix hello salve-mundi -c hello | grep 'Hello World'
    nix shell -f shell-hello.nix salve-mundi hello -c hello | grep 'Salve Mundi'
fi

requireSandboxSupport

chmod -R u+w $TEST_ROOT/store0 || true
rm -rf $TEST_ROOT/store0

clearStore

path=$(nix eval --raw -f shell-hello.nix hello)

# Note: we need the sandbox paths to ensure that the shell is
# visible in the sandbox.
nix shell --sandbox-build-dir /build-tmp \
    --sandbox-paths '/nix? /bin? /lib? /lib64? /usr?' \
    --store $TEST_ROOT/store0 -f shell-hello.nix hello -c hello | grep 'Hello World'

path2=$(nix shell --sandbox-paths '/nix? /bin? /lib? /lib64? /usr?' --store $TEST_ROOT/store0 -f shell-hello.nix hello -c $SHELL -c 'type -p hello')

[[ $path/bin/hello = $path2 ]]

[[ -e $TEST_ROOT/store0/nix/store/$(basename $path)/bin/hello ]]

# Test whether `nix shell` sets `IN_NIX_SHELL` appropriately
[[ "$(nix shell -f shell-hello.nix -c sh -c 'echo $IN_NIX_SHELL')" == impure ]]
[[ "$(nix shell --ignore-environment -f shell-hello.nix -c /bin/sh -c 'echo $IN_NIX_SHELL')" == pure ]]
