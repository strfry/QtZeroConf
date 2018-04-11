qt5gui = dependency('qt5', modules: ['Gui'])

moc_headers = files(
	'window.h'
)

moc_files = qt5.preprocess(moc_headers: moc_headers)


sources = files('main.cpp', 'window.cpp')


executable('testapp', sources, moc_files,
	dependencies: [qtzeroconf_dep, qt5gui])
