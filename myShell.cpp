#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <ctype.h>
#include <vector>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <string>
#include <fcntl.h>
#include <string.h>

using namespace std;

vector<string> commandHistory;
string currentDirectory;

void ResetCanonicalMode(int fd, struct termios *savedattributes) {
    tcsetattr(fd, TCSANOW, savedattributes);
}

void SetNonCanonicalMode(int fd, struct termios *savedattributes) {
    struct termios TermAttributes;
    char *name;

    // Make sure stdin is a terminal.
    if (!isatty(fd)) {
        fprintf (stderr, "Not a terminal.\n");
        exit(0);
    }

    // Save the terminal attributes so we can restore them later.
    tcgetattr(fd, savedattributes);

    // Set the funny terminal modes.
    tcgetattr (fd, &TermAttributes);
    TermAttributes.c_lflag &= ~(ICANON | ECHO); // Clear ICANON and ECHO.
    TermAttributes.c_cc[VMIN] = 1;
    TermAttributes.c_cc[VTIME] = 0;
    tcsetattr(fd, TCSAFLUSH, &TermAttributes);
}

string readCommand()
{
    char RXChar;
    string buffer = "";
    int historyTraversalCounter = -1;

    while (1) {
        read(STDIN_FILENO, &RXChar, 1);
        if (RXChar == 0x0A) // Enter
        {
            write(STDOUT_FILENO, "\n", 1);
            break;
        }
        else if (RXChar == 0x04) // CTRL D
            exit(0);
        else if (RXChar == 0x1B) //ESC
        {
            read(STDIN_FILENO, &RXChar, 1);
            if (RXChar == 0x5B) {
                read(STDIN_FILENO, &RXChar, 1);
                if (RXChar == 0x41 || RXChar == 0x42) { //UP ARROW OR DOWN ARROW

                    //Bell if out of bounds
                    if ((historyTraversalCounter == commandHistory.size() - 1 && RXChar == 0x41)
                            || (historyTraversalCounter < 0 && RXChar == 0x42))
                        write(STDOUT_FILENO, "\a", 1);
                    else {
                        //clear screen and buffer
                        int bufferSize = buffer.length();
                        buffer.clear();

                        for (int i = 0; i < bufferSize; ++i)
                            write(STDOUT_FILENO, "\b \b", 3);

                        if (RXChar == 0x42)
                            if (--historyTraversalCounter < 0)
                                continue;

                        if (RXChar == 0x41 )
                            historyTraversalCounter++;

                        //set buffer to history command and write to screen
                        buffer = commandHistory[historyTraversalCounter];
                        write(STDOUT_FILENO, buffer.c_str(), buffer.length());
                    }
                    continue;
                }
                else //right arrow or left arrow
                    continue;
            }
        }
        if (isprint(RXChar)) {
            write(STDOUT_FILENO, &RXChar, 1);
            buffer += RXChar;
        }
        else {

            if (RXChar == 0x7F || RXChar == 0x2E) { //backspace and delete
                if (!buffer.empty()) {
                    write(STDOUT_FILENO, "\b \b", 3);
                    buffer.erase(buffer.end() - 1);
                }
                else
                    write(STDOUT_FILENO, "\a", 1);

            }
        }
    }

    return buffer;
}

string formatDirOutput(string dir)
{
    if (dir.length() <= 16)
        return dir + "> ";

    string last = dir.substr(dir.find_last_of('/') + 1, dir.length());
    return "/.../" + last + "> ";
}

vector<string> parseCommand(string cmd)
{
    string buffer = "";
    vector<string> parsedCmd;                   // tokenize commands into a vector

    for (int i = 0; i < cmd.length(); i++) {
        if (cmd[i] != ' ' && cmd[i] != '>' && cmd[i] != '<' && cmd[i] != '|')
            buffer += cmd[i];
        else {
            if (!buffer.empty()) {
                parsedCmd.push_back(buffer);
                buffer.clear();
            }

            if (cmd[i] != ' ')
                parsedCmd.push_back(string(1, cmd[i]));
        }
    }

    if (!buffer.empty())
        parsedCmd.push_back(buffer);

    return parsedCmd;
}

string genPermString(struct stat filePermissions)
{
    string s = "";
    if (S_ISDIR(filePermissions.st_mode)) s += "d"; else s += "-";
    if (filePermissions.st_mode & S_IRUSR) s += "r"; else s += "-";
    if (filePermissions.st_mode & S_IWUSR) s += "w"; else s += "-";
    if (filePermissions.st_mode & S_IXUSR) s += "x"; else s += "-";
    if (filePermissions.st_mode & S_IRGRP) s += "r"; else s += "-";
    if (filePermissions.st_mode & S_IWGRP) s += "w"; else s += "-";
    if (filePermissions.st_mode & S_IXGRP) s += "x"; else s += "-";
    if (filePermissions.st_mode & S_IROTH) s += "r"; else s += "-";
    if (filePermissions.st_mode & S_IWOTH) s += "w"; else s += "-";
    if (filePermissions.st_mode & S_IXOTH) s += "x"; else s += "-";
    return s;
}

void funcLS(vector<string> cmd) {
    DIR *mydir;
    struct dirent *file;
    struct stat filePermissions;
    const char* dir = ".";
    string errString  = "Failed to open directory ";

    if (cmd.size() > 1)
        dir = cmd[1].c_str();

    mydir = opendir(dir);

    if (mydir != NULL) {
        while ((file = readdir(mydir)) != NULL)
        { 
            string str = dir;
            str += "/";
            str += file->d_name;

            string printStr(file->d_name);

            stat(str.c_str(), &filePermissions);  // no error checking needed, we know file exists

            string sPerm = genPermString(filePermissions) + " ";
            write(STDOUT_FILENO, sPerm.c_str(), sPerm.length());
            write(STDOUT_FILENO, printStr.c_str(), printStr.length());
            write(STDOUT_FILENO, "\n", 1);
        }
        closedir(mydir);
    }
    else {
        errString = errString + "\"" + dir + "/\"\n";
        write(STDOUT_FILENO, errString.c_str(), errString.length());
    }
}

void funcCD(vector<string> cmd) {
    string errMsg = "Error changing directory.\n";
    const char* dir = getenv("HOME");
    if (cmd.size() > 1)
        dir = cmd[1].c_str();
    if (chdir(dir) != 0) // unable to open
        write(STDOUT_FILENO, errMsg.c_str(), errMsg.length());
}

void funcPWD(vector<string> cmd) {
    string dir = get_current_dir_name();
    write(STDOUT_FILENO, dir.c_str(), dir.length());
    write(STDOUT_FILENO, "\n", 1);
}

void funcHISTORY(vector<string> cmd) {
    string buffer;
    for (int i = 0; i < commandHistory.size(); i++) {
        buffer = "";
        buffer += char(i + '0');
        buffer += ' ' + commandHistory[commandHistory.size() - i - 1];
        write(STDOUT_FILENO, buffer.c_str(), buffer.length());
        write(STDOUT_FILENO, "\n", 1);
    }
}

void funcCall(vector<string> cmd) {
    int    cmdSize;
    char** arg_array;   // cmd vector in char** form, used for execvp

    cmdSize = cmd.size();
    arg_array = new char*[cmdSize + 1];

    for (int i = 0; i < cmdSize; i++)
        arg_array[i] = (char*)cmd[i].c_str();

    arg_array[cmdSize] = 0;

    execvp(arg_array[0], arg_array);
    exit(1);
}

void executeCommand(vector<string> cmd) {
    //cmdList is a partitioning of cmd where the partitions are separated by a '|'
    vector< vector<string> > cmdList;

    vector<string> temp;
    vector<int*> pipes;

    //create vector of grouped commands
    for (int i = 0; i < cmd.size(); i++) {
        if (cmd[i].compare("|") == 0) {
            //push parsed command into cmdList when | is found and clear temp
            cmdList.push_back(temp);
            temp.clear();

            //skip over '|' character
            i++;
        }

        temp.push_back(cmd[i]);
    }

    cmdList.push_back(temp);

    pid_t  pid;
    int prevPipe[2];
    int nextPipe[2];
    int status;

    for (int i = 0; i < cmdList.size(); i++) {

        //create copy of cmdList[i] and store into oneCmd
        vector<string> oneCmd;
        for (int j = 0; j < cmdList[i].size(); j++)
            oneCmd.push_back(cmdList[i][j]);

        //cd command
        if (oneCmd[0] == "cd" && i == 0) {
            funcCD(oneCmd);
            return;
        }

        //exit command
        if (oneCmd[0] == "exit" && i == 0)
            exit(0);

        //pipe if next command exists
        if(i != cmdList.size()-1) //has next cmd
            pipe(nextPipe);

        pid = fork();

        if (pid == 0) {         // if child
            vector<string> execCmd;
            
            //setting up the redirects and creates the executable command
            int redirectFD;
            
            for (int j = 0; j < oneCmd.size(); j++) {
                if (oneCmd[j] != "<" && oneCmd[j] != ">") {
                    execCmd.push_back(oneCmd[j]);
                }
                else {
                    j++;
                    if (oneCmd[j - 1] == ">") {
                        //outFD
                        redirectFD = open(oneCmd[j].c_str(), O_CREAT | O_TRUNC | O_WRONLY, S_IRUSR | S_IROTH | S_IRGRP | S_IWUSR);
                        dup2(redirectFD, STDOUT_FILENO);
                    }
                    else {
                        //inFD
                        if ((redirectFD = open(oneCmd[j].c_str(), O_RDONLY)) < 0) {
                            string errMsg = "File \"" + oneCmd[j] + "\"  does not exist!\n";
                            write(STDOUT_FILENO, errMsg.c_str(), errMsg.length());
                            exit(0);
                        }
                        else
                            dup2(redirectFD, STDIN_FILENO);
                    }
                    close(redirectFD);
                }
            }

            if (i > 0){                    // Has a prev
                dup2(prevPipe[0], STDIN_FILENO);
                close(prevPipe[0]);
                close(prevPipe[1]);
            }

            if (i != cmdList.size() - 1){       // Has a next
                dup2(nextPipe[1], STDOUT_FILENO);
                close(nextPipe[0]);
                close(nextPipe[1]);
            }

            //execute command
            if (execCmd[0] == "ls") funcLS(execCmd);
            else if (execCmd[0] == "cd") funcCD(execCmd);
            else if (execCmd[0] == "pwd") funcPWD(execCmd);
            else if (execCmd[0] == "history") funcHISTORY(execCmd);
            else funcCall(execCmd);
            exit(0);
        }

        //closing the prevPipe of the parent if it exists
        if(i > 0){                    // Has a prev
            close(prevPipe[0]);
            close(prevPipe[1]);
        }

        //closing the nextPipe of the parent if it exists
        if(i != cmdList.size() - 1){ //has next cmd
            prevPipe[0] = nextPipe[0];
            prevPipe[1] = nextPipe[1];
        }

        waitpid(pid, &status, 0);

        //error message for failure to execute
        string errString = "Failed to execute ";
        if (WIFEXITED(status) && WEXITSTATUS(status)) {  // If exit status == 1 error, else 0 = success
            errString = errString + oneCmd[0] + "\n";
            write(STDOUT_FILENO, errString.c_str(), errString.length());
        }
    }
}

int main(int argc, char *argv[]) {
    struct termios SavedTermAttributes;
    SetNonCanonicalMode(STDIN_FILENO, &SavedTermAttributes);
    string formattedDir;

    while (1)
    {
        formattedDir = formatDirOutput(get_current_dir_name());
        write(STDOUT_FILENO, formattedDir.c_str(), formattedDir.length());
        string cmd = readCommand();

        if (commandHistory.size() == 10)
            commandHistory.pop_back();

        commandHistory.insert(commandHistory.begin(), cmd);
        vector<string> cmdVector = parseCommand(cmd);
        executeCommand(cmdVector);
    }
    return 0;
    ResetCanonicalMode(STDIN_FILENO, &SavedTermAttributes);
}

