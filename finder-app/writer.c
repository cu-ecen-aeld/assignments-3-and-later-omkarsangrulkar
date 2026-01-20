#include <stdio.h>
#include <stdlib.h>
#include <syslog.h>
#include <string.h>
#include <errno.h>

int main(int argc, char *argv[]) {
    // Open syslog with LOG_USER facility
    openlog("writer", LOG_PID, LOG_USER);
    
    // Check if both arguments are provided
    if (argc != 3) {
        syslog(LOG_ERR, "Invalid number of arguments: %d", argc - 1);
        fprintf(stderr, "Error: Missing arguments.\n");
        fprintf(stderr, "Usage: %s <writefile> <writestr>\n", argv[0]);
        closelog();
        return 1;
    }
    
    const char *writefile = argv[1];
    const char *writestr = argv[2];
    
    // Log the write operation with LOG_DEBUG level
    syslog(LOG_DEBUG, "Writing %s to %s", writestr, writefile);
    
    // Open the file for writing
    FILE *fp = fopen(writefile, "w");
    if (fp == NULL) {
        syslog(LOG_ERR, "Could not open file %s for writing: %s", 
               writefile, strerror(errno));
        fprintf(stderr, "Error: Could not create or write to file %s\n", writefile);
        closelog();
        return 1;
    }
    
    // Write the string to the file
    if (fprintf(fp, "%s", writestr) < 0) {
        syslog(LOG_ERR, "Could not write to file %s: %s", 
               writefile, strerror(errno));
        fprintf(stderr, "Error: Could not write to file %s\n", writefile);
        fclose(fp);
        closelog();
        return 1;
    }
    
    // Close the file
    if (fclose(fp) != 0) {
        syslog(LOG_ERR, "Could not close file %s: %s", 
               writefile, strerror(errno));
        fprintf(stderr, "Error: Could not close file %s\n", writefile);
        closelog();
        return 1;
    }
    
    // Close syslog
    closelog();
    
    return 0;
}
