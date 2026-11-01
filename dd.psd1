@{
    schema = 1
    project = @{
        name = 'zpilot'
        type = 'gui'
        'default-target' = 'app'
    }
    dependencies = @{ owner = 'application' }
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
            'cmake-target' = 'zpilot'
            'test-label' = 'zpilot'
            'debug-path' = 'build/debug/zpilot{exe}'
            'release-path' = 'build/release/zpilot{exe}'
            platforms = @('x64-windows')
        }
    )
}
