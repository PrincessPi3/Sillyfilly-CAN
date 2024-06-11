function Test-Delete {
    param (
        $file
    )

    if (Test-Path $file) { 
        rm -r -fo $file
        Write-Output "$file DELETED" 
     } else {
        Write-Output "$file NOT present"
     }
}

idf.py erase-flash fullclean python-clean

Test-Delete -file .\managed_components
Test-Delete -file .\build
Test-Delete -file .\sdkconfig.old
Test-Delete -file .\dependencies.lock
Test-Delete -file .\sdkconfig.old
