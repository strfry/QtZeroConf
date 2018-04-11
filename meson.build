project('QtZeroConf', 'cpp')

qt5 = import('qt5')
qt5_dep = dependency('qt5', modules: ['Core', 'Gui', 'Network', 'Widgets'])

avahi_dep = dependency('avahi-client')

inc = include_directories('.')

moc_headers = files(
    'qzeroconf.h',
    'avahi-qt/qt-watch.h',
    'avahi-qt/qt-watch_p.h',
    'qzeroconfservice.h',
    'qzeroconfglobal.h',
)

sources = files(
    'avahiclient.cpp',
    'avahi-qt/qt-watch.cpp',
    'qzeroconfservice.cpp',

)

moc_files = qt5.preprocess(moc_headers: moc_headers)

lib = static_library('QtZeroConf',
    moc_files, sources,
    dependencies: qt5_dep
    )

qtzeroconf_dep = declare_dependency(
    include_directories: inc,
    link_with: lib,
    dependencies: [avahi_dep, qt5_dep],
)

subdir('example')
