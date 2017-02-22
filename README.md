


# keyring

> Access the System Credential Store from R

[![Linux Build Status](https://travis-ci.org/gaborcsardi/keyring.svg?branch=master)](https://travis-ci.org/gaborcsardi/keyring)
[![Windows Build status](https://ci.appveyor.com/api/projects/status/github/gaborcsardi/keyring?svg=true)](https://ci.appveyor.com/project/gaborcsardi/keyring)
[![](http://www.r-pkg.org/badges/version/keyring)](http://www.r-pkg.org/pkg/keyring)
[![CRAN RStudio mirror downloads](http://cranlogs.r-pkg.org/badges/keyring)](http://www.r-pkg.org/pkg/keyring)

Platform independent API to many system credential store implementations.
Currently supported: Keychain on 'macOS', Credential Store on 'Windows',
the Secret Service API and the Gnome keyring API on 'Linux'.

## Installation


```r
source("https://install-github.me/gaborcsardi/keyring")
```

## Usage


```r
library(keyring)
```

## License

MIT © RStudio
