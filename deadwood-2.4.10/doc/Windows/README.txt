Deadwood is a program that allows one to have a simple DNS cache on their
Windows system.  

These programs use the command-line to install the program, and assume
basic familiarity with the Windows command line.  The fastest way to get
to a command prompt in Windows XP is Start -> Run -> "cmd"

Before you can install the Deadwood service in Windows Vista or Windows 7, 
please read the file Vista.txt

Deadwood is a Windows service.  This means that the program needs to be 
installed as a service before being used.  First, find a suitable directory 
on your file system to install Deadwood, such as "c:\Program Files\Deadwood\"

Once Deadwood.exe is placed in this directory, install the service.  Type
in the following at a command prompt in the same directory as 
Deadwood.exe:

        Deadwood --install 

For security reasons, you need to make a file with random text in it called 
secret.txt.  One way to do this is to type in "notepad secret.txt", type in 
random gibberish, and save the file.

Once these files are properly configured, and the Deadwood service is
installed, Deadwood can be started:

        net start Deadwood

(You can also start it from Control Panel -> Administrative Tools -> Services)

Deadwood, at this point, should automatically start whenever the system is 
booted.  

The file dwood2rc.txt can be edited to change the upstream DNS servers used
(Google's public DNS servers by default), as well as a number of other
options that are described in Reference.txt.

Deadwood uses a file to store messages called "dwlog.txt" (without the quotes)
in the same directory where Deadwood is started.  If there are any errors
that make it so Deadwood can not start, they should be noted in this log
file.  

To stop Deadwood:

        net stop Deadwood

(Or from the Services control panel if you prefer mousing it)

Deadwood will write its cache to a file (if specified) when stopped as a
service.

In order to actually use the Deadwood DNS cache on your computer, go
to the control panel entry for network connections (Control Panel ->
Network connections), then right-click on the network connection you use
and select "properties", select the TCP/IP protocol in the list of network
types in the window, click on the button marked "properties", manually
select DNS servers, and make 127.0.0.1 the DNS server used.

It is also possible to remove the Deadwood service:

        Deadwood --remove 

if one wishes to uninstall Deadwood.  Be sure to reset the DNS servers used
before uninstalling Deadwood, otherwise it won't be possible to use the
internet.

A full manual for Deadwood, based on Deadwood's CentOS Linux manual, is in 
the file Reference.txt

LEGAL DISCLAIMER

THIS SOFTWARE IS PROVIDED BY THE AUTHORS ''AS IS'' AND ANY
EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHORS
OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
THE POSSIBILITY OF SUCH DAMAGE.
