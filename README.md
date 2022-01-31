# FastReport Cloud console shell #

This is a simple console shell to [FastReport Cloud](https://fastreport.cloud/ "FastReport.Cloud") service.

### Prerequests GNU packages for build shell: ### 
<ul>
<li>curl-development</li>
<li>gnutls-development</li>
<li>readline-development</li>
</ul>     

Theses packages provide libraries and headers for building cloud console. Name of packages may vary depends on your distro. Debian example:
 ```bash
 apt-get install libcurl4-gnutls-dev
 apt-get install gnutls-dev
```

Cloud shell source code includes some parts of https://github.com/zserge/jsmn.git 

### Build: ###  

type ```make``` in project directory for build executable  
type ```strip frcloud``` for reducing binary size  
  
### Running ###

type ```./frcoud``` for run console

If token file not found then programm will ask for token. Provide correct token and it will be stored to a configuration file. 


