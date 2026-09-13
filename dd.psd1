@{
    schema = 1
    project = @{
        name = 'z-pilot'
        type = 'gui'
        'default-target' = 'app'
    }
    dependencies = @{ owner = 'dd' }
    build = @{
        'x64-windows' = @{
            debug = 'debug'
            release = 'release'
        }
    }
    targets = @(
        @{
            id = 'app'
            kind = 'gui'
            'cmake-target' = 'z-pilot'
            'test-label' = 'z-pilot'
            'debug-path' = 'build/debug/z-pilot{exe}'
            'release-path' = 'build/release/z-pilot{exe}'
            platforms = @('x64-windows')
        }
    )
}
