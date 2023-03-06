# Tutorial

The goal of this tutorial is to introduce you to the Augyang project and also to give you a broader perspective on how Augyang fits into the puzzle of concepts around [sysrepo](https://www.sysrepo.org/).
Some steps in the tutorial may be optional for you, you don't have to do them, but it's good to know about them.
Those of you who are a bit familiar with the projects of [libyang](https://netopeer.liberouter.org/doc/libyang/master/html/), [sysrepo](https://netopeer.liberouter.org/doc/sysrepo/master/html/) and [netopeer2](https://github.com/CESNET/netopeer2) can focus on the [Augeas](#augeas) and [Augyang](#augyang) chapters and even the [datastore plugin](#datastore-plugin) and [sysrepoctl](#sysrepoctl) headings.
To try all the steps in the tutorial you need to install [libyang](https://github.com/CESNET/libyang) first, then [augeas](https://github.com/hercules-team/augeas), augyang, [sysrepo](https://github.com/sysrepo/sysrepo), [libnetconf2](https://github.com/CESNET/libnetconf2) and [netopeer2](https://github.com/CESNET/netopeer2).
Keep in mind that for the libyang, sysrepo, libnetconf2 and netopeer2 projects to work properly, the latest stable versions should be installed, or perhaps a *devel* branch could be used.
One more thing, the Augyang project contains fixes for the *augeas modules* (lenses), so to run the `make -C ./build tests` successfully, the  `make -C ./build install_lens` command needs to be called beforehand to modify the installed *augeas-lenses*.

## Augeas

On the official [Augeas website](https://augeas.net/) you can read that:

Augeas is a configuration editing tool.
It parses configuration files in their native formats and transforms them into a **tree**.
Configuration changes are made by manipulating this **tree** and saving it back into native configuration files.

Thanks to Augyang the configuration file can be described in YANG format and the mentioned **tree** can then be manipulated with *sysrepo* cooperating with Augeas.

In this chapter we will also introduce [augparse](#augparse), [augmatch](#augmatch) and [augtool](#augtool) tools belonging to Augeas.

### augeas module

To parse a configuration file, Augeas needs to know its format.
The description of this format is achieved by means of so-called *lenses* and their integrity is stored in the *augeas module*.
You can see the list of written *augeas modules* [here](https://github.com/hercules-team/augeas/tree/master/lenses).
The *augyang tool* is able to translate all these *augeas modules* into *YANG modules*, as shown [here](https://github.com/telekom/sysrepo-augeas/tree/main/tests/yang_expected).

For this tutorial we will create our own *augeas module*:

```aug
module Ay_lense_example =
  autoload xfm

  let variable_name = Rx.word
  let assignment = del /[ \t]*<-[ \t]*/ " <- "
  let number = Rx.integer
  let line = [key variable_name . assignment . store number] . Util.eol
  let lns = (line | Util.empty)*

  let filter = incl "/tmp/ay_data_example.txt"
  let xfm = transform lns filter
```

And let's save this *augeas module* to an `ay_lense_example.aug` file.

This particular *lense* we created as an example describes a configuration file in which the line consists of a variable name, followed by an assignment delimiter, followed by a numeric value.
You can check the [augeas documentation](https://augeas.net/docs/lenses.html) or the [wiki pages](https://github.com/hercules-team/augeas/wiki) to closer understanding *lenses* language.
The configuration file is is specifically located in `/tmp/ay_data_example.txt`.
Note that in order for *augyang* to produce a nice YANG file, the author of the *augeas module* should create *lenses* so that most of them have a meaningful identifier.

### augparse

The *augparse tool* is used to validate the *augeas module*.

```bash
augparse -I . ay_lense_example.aug
```

If the module is OK, the command does not print anything.
With the `-I .` option, we told *augparse* to look for our file in the current directory.
By default, it also looks in the directory where all *augeas modules* are installed and there it finds the **Rx** and **Util** *lenses* that our Ay_lense_example module depends on.
If the *augeas module* is too complex and large, validation can take a long time, so basic validation can be done using the `--notypecheck` option.
When developing an *augeas module*, it is also important to create [lense-tests](https://github.com/hercules-team/augeas/tree/master/lenses/tests), but we will skip that step in this tutorial.

### configuration file

In the [augeas module](#augeas-module) section we have created a description of how to parse the configuration file and where to find it.
So let's create this file now, so we can see how we can change it later using different tools.
Write the text in the `/tmp/ay_data_example.txt` file:

```txt
a <- 4
b <- 3
```

### augmatch

The *augmatch* tool will help us verify that our Ay_lense_example *augeas module* can parse the `ay_data_example.txt` configuration file.

```
augmatch -I . /tmp/ay_data_example.txt
```

By option `-I .`, our *augeas module* will be found in the current directory and lastly we enter the path to our configuration file.
The output is then:

```
a = 4
b = 3
```

The output is divided into two columns separated by the `=` sign.
The first column contains the path to the node and the second column contains the corresponding value.
However, since our nodes have no parent node, its name is displayed directly.

### augtool

This tool is not important from an Augyang perspective, but we can use it to demonstrate how Augeas manipulates the **augeas tree**.
As already mentioned at the beginning of the [Augeas](#augeas) chapter, Augeas parses the configuration file and creates a **tree** from it.
Let's run *augtool*:

```bash
augtool -I . --noload --load-file /tmp/ay_data_example.txt
```

It may take a while, because *augtool* will load all *augeas modules* including ours `ay_lense_example.aug` thanks to the `-I .` option.
Then we used the `--noload` option to forbid it to load the configuration files, but the `--load-file` switch was used to add that it should load our configuration file `ay_data_example.txt` after all.

Let's check that our module is loaded:

```
augtool> print /augeas/load/Ay_lense_example/
/augeas/load/Ay_lense_example
/augeas/load/Ay_lense_example/lens = "@Ay_lense_example"
/augeas/load/Ay_lense_example/incl = "/tmp/ay_data_example.txt"
```

And now we can show the **augeas tree** for the `ay_data_example.txt` configuration file.

```
augtool> print /files/tmp/ay_data_example.txt/
/files/tmp/ay_data_example.txt
/files/tmp/ay_data_example.txt/a = "4"
/files/tmp/ay_data_example.txt/b = "3"
```

And to demonstrate **tree** manipulation, let's add another node:

```
augtool> set /files/tmp/ay_data_example.txt/c 5
augtool> save
Saved 1 file(s)
```

And our file `/tmp/ay_data_example.txt` should now look like this:

```txt
a <- 4
b <- 3
c <- 5
```

Finally, return the configuration file to its original form by removing the added node:

```
augtool> rm /files/tmp/ay_data_example.txt/c
rm : /files/tmp/ay_data_example.txt/c 1
augtool> save
Saved 1 file(s)
```

## Augyang

The main result of the Augyang project are two binaries.
The first is the [augyang tool](#augyang) for converting an *augeas module* to a *YANG module*.
The second is the [sysrepo datastore plugin](#datastore-plugin) `srds_augeas.so` for manipulating configuration data.
Another important output is the `modules/augeas-extension.yang` module, which is basic extension for all generated YANG files.
In addition, we will also introduce the [ay_startup](#ay_startup) tool, which can be useful for a basic check that things are working.

### augyang

The input to the *augyang* tool is a single *augeas module* for which a *YANG module* is to be created, and the input is also the set of complementary *augeas modules* on which the module depends.
In short, the *augyang* tool works by letting Augeas parse and compile the *lenses* as it usually does.
It then uses these compiled *lenses* for its own transformations, the output of which is a YANG file.
So it doesn't need to know the configuration file at all, but it needs to have [valid](#augparse) *augeas modules*.
The challenge for *augyang* is to create a hierarchy of yang nodes that roughly corresponds to the description of the configuration file similar to *lenses*.
At the same time, it must also *tag* the yang nodes so that it is possible to determine exactly which place in the **augeas tree** the node refers to.
This is achieved using an extension from the `augaes-extension.yang` module.
These *tags*/extension-statements actually create a mapping between the **yang tree** and the **augeas tree**.
And this mapping allows the [sysrepo plugin](#datastore-plugin) to do its job.

### created YANG module

Let's create a YANG file from an `ay_lense_example.aug` file.

```bash
build/augyang -I . -y ay_lense_example
```

The result is a `ay-lense-example.yang` file.
The `-I .` option allowed us to search the current directory where `ay_lense_example.aug` is supposed to be located there.
From the augeas module Ay_lense_example, the dependencies of the augeas modules **Rx** and **Util** were found, which were automatically found in the `/usr/share/augeas/lenses/dist` directory where they are installed.
The last `-y` option did the validation of the resulting YANG file.

Let's take a look at the `ay-lense-example.yang` file:

```yang
module ay-lense-example {
  yang-version 1.1;
  namespace "aug:ay-lense-example";
  prefix aug;

  import augeas-extension {
    prefix augex;
  }

  augex:augeas-mod-name "Ay_lense_example";

  list ay-lense-example {
    key "config-file";
    leaf config-file {
      type string;
    }
    list variable-name-list {
      key "_id";
      ordered-by user;
      leaf _id {
        type uint64;
        description
          "Implicitly generated list key to maintain the order of the augeas data.";
      }
      container variable-name {
        augex:data-path "$$";
        augex:value-yang-path "number";
        presence "Config entry.";
        leaf variable-name {
          mandatory true;
          type string {
            pattern "[A-Za-z0-9_.-]+";
          }
        }
        leaf number {
          mandatory true;
          type uint64;
        }
      }
    }
  }
}
```

First, the `augeas-extension` module is always imported and contains extension that are used to *tag* yang nodes.
By *tagging* is meant the `augeas:` extension.
The `list ay-lense-example` is a list where the key is the path to the configuration file.
A single *augeas module* can in fact be used for multiple configuration files, but for simplicity, this tutorial only works with one.
The `list variable-name-list` is used to describe the order of each record, as this can be very important in a configuration file.
The `container variable-name` finally describes specific data.
The `augex:data-path "$$"` statement indicates that the name of the data node is not predefined because it comes from a string somewhere in the configuration file.
Also that name must match a regular expression found in the `leaf variable-name` node.
The `augex:value-yang-path "number"` statement announces that a value is also bound to the node and must match a regular expression found in the `leaf number` node.
More detailed documentation about the extension can be found directly in the `augeas-extension.yang`.

The YANG file you create can be viewed as a template that you can modify to some extent.
you can freely edit the identifiers of yang nodes to increase the readability of [xml data](#ay_startup).
Shorten the total length of the module using a grouping statements.
You can also freely add description statements to nodes.
Or even truncate regular expressions so that they are actually identical.
You can probably edit even more, but be careful not to break the potential mapping between the **yang tree** and the **augeas tree**, for example.
Also note that *augyang* doesn't take your edits into account, so you may have to deal with changes again when you update *augyang*.

### ay_startup

This tool comes in handy when you need to quickly determine if an *augeas-lense* and corresponding YANG file is practically usable by *sysrepo*.
Based on the given YANG file, it finds the corresponding *augeas module*, also finds the configuration files, then builds an **augeas tree** which maps to the YANG data model and finally prints the xml data.
To do all this, it uses the *sysrepo* and *augeas APIs*.

To test the tool, we first need to install our augeas module by simply copying it:

```bash
sudo cp ay_lense_example.aug /usr/share/augeas/lenses/dist/
```

In the previous step of this tutorial, we have already created a [configuration file](#configuration-file).
So we can test the tool:

```bash
build/ay_startup ay-lense-example
```

The tool implicitly searches the current and the `tests/yang_expected/` directories, so you only need to enter the *YANG module* name.
The output should then be:

```xml
<ay-lense-example xmlns="aug:ay-lense-example">
  <config-file>/tmp/ay_data_example.txt</config-file>
  <variable-name-list>
    <_id>1</_id>
    <variable-name>
      <variable-name>a</variable-name>
      <number>4</number>
    </variable-name>
  </variable-name-list>
  <variable-name-list>
    <_id>2</_id>
    <variable-name>
      <variable-name>b</variable-name>
      <number>3</number>
    </variable-name>
  </variable-name-list>
</ay-lense-example>
```

Which corresponds to the contents of the `/tmp/ay_data_example.txt` configuration file.

## Sysrepo

[Sysrepo](https://netopeer.liberouter.org/doc/sysrepo/master/html/index.html) provide standards compliant implementation of a NETCONF server and YANG configuration data stores. 
Thanks to its flexibility, it can be controlled by user plugins and can also be adapted to use Augeas.
For this it needs only one plugin and that is the [datastore plugin](#datastore-plugin) `srds_augeas.so`.
And of course it also needs the *YANG module*, which will provide the [augyang](#augyang) tool.
In this chapter, we will also try out the [sysrepoctl](#sysrepoctl) tool for managing *YANG modules* and the [sysrepocfg](#sysrepocfg) tool for modifying configuration file.
These tools are optional, as the same results can be achieved using *sysrepo API*, yet their use within the tutorial will be more straightforward.

### datastore plugin

The `srds_augeas.so` plugin is actually a shared library ready for installation.
Define a set of callbacks that implement all the operations performed on a datastore.
Specifically, it is customized for the [Startup Configuration Datastore](https://www.rfc-editor.org/rfc/rfc8342#section-5.1.1), which by definition is supposed to hold the configuration for a potential device at boot time.

### sysrepoctl

The *sysrepoctl* tool will help us manage the *YANG modules*.
Before trying the *sysrepoctl* tool, let's also note that if *sysrepo* gets into an inconsistent state, it can be reset to its original state using the `make -C build/ sr_clean` command in the *sysrepo* source repository.
Now let's try to set up *sysrepo* from the Augyang repository.
First let's install the [srds_augeas](#datastore-plugin) plugin.

```bash
sudo sysrepoctl -P build/srds_augeas.so
```

The plugin is simply copied to the designated plugin directory.
The plugin does not need to be reinstalled if we install the *YANG module* again, or if we add another *YANG module*, etc.
We can check its availability by:

```bash
sysrepoctl -L
```

Where, among other things, the `augeas DS` string should appear.
Now let's install [our module](#created-yang-module) created by [augyang](#augyang):

```bash
sysrepoctl -i modules/augeas-extension.yang -i ay-lense-example.yang -m startup:"augeas DS"
```

In the command we can see that we must not forget the auxiliary *YANG module* containing the extension.
We bind our modules with the `-m` option to the **startup** datastore and also to the `srds_augeas` plugin.

### sysrepocfg

The *sysrepocfg* tool allows data manipulation.
If everything went well, we can look at the configuration data in a similar way as with the [ay_startup](#ay_startup) tool:

```bash
sysrepocfg -X -d startup -m ay-lense-example 
```

In the command, we specified the export operation, entered the **startup** datastore and the name of the our module.
We should get the output:

```xml
<ay-lense-example xmlns="aug:ay-lense-example">
  <config-file>/tmp/ay_data_example.txt</config-file>
  <variable-name-list>
    <_id>1</_id>
    <variable-name>
      <variable-name>a</variable-name>
      <number>4</number>
    </variable-name>
  </variable-name-list>
  <variable-name-list>
    <_id>2</_id>
    <variable-name>
      <variable-name>b</variable-name>
      <number>3</number>
    </variable-name>
  </variable-name-list>
</ay-lense-example>
```

Now let's modify the data similar to what we did with [augtool](#augtool):

```bash
sysrepocfg --edit=vim -d startup -m ay-lense-example
```

The *vim* editor was specified for editing but of course it can be any other.
And in it we change the name of the variable `a` to `myverylongandshinyvariable`.
We can then check the `/tmp/ay_data_example.txt` file to see if the changes have been applied:

```txt
myverylongandshinyvariable <- 4
b <- 3
```

Great, so we changed the configuration file using the *sysrepo* tools.
Of course, we can also add and remove entries in the `variable-name-list` list as we like.
However, the user must maintain the `_id` keys himself.

## NETCONF

In this chapter, we will start the [netopeer2](https://github.com/CESNET/netopeer2) server and edit our configuration file remotely using the client.
We're going to rely on the work we've already done in the previous steps.
That is, we converted the [augeas module](#augeas-module)
to a [YANG module](#created-yang-module) by [augyang](#augyang) and then installed that module using [sysrepoctl](#sysrepoctl).

### netopeer-server

The *netopeer2* server uses the [libnetconf2](https://netopeer.liberouter.org/doc/libnetconf2/devel/html/index.html) library, which implements [NETCONF](https://trac.ietf.org/trac/netconf/wiki) for two-way communication with the client.
For communication to work, the NETCONF *YANG modules* must be installed in *sysrepo*.
The first thing to do would be to reset the shared memory.
To do this, you can use the `make -C build shm_clean` command, which you must run in the [sysrepo repository](https://github.com/sysrepo/sysrepo).
You can then run the `sudo make install` command in the `build` directory of the [netopeer2](https://github.com/CESNET/netopeer2) repository, which will automatically install the NETCONF modules.

Let's start the server in a new terminal window.

```
sudo netopeer2-server -U -d -v 1
```

Using the `-U` option, the server will listen on the local UNIX socket.
The `-d` and `-v 1` options will cause information about potential problems to be displayed.

### netopeer-cli

Before starting the client, we first prepare a new form of our configuration file:

```xml
<ay-lense-example xmlns="aug:ay-lense-example">
  <config-file>/tmp/ay_data_example.txt</config-file>
  <variable-name-list>
    <_id>1</_id>
    <variable-name>
      <variable-name>remotely_changed</variable-name>
      <number>564</number>
    </variable-name>
  </variable-name-list>
</ay-lense-example>
```

And save it as an `aydata.xml`.
With this data we should change the first line of the configuration file. The second line should remain intact.
Let's start the client:

```
sudo netopeer2-cli
```

We are now in interactive client mode.
Let's connect to the server via the local Unix socket:

```
> connect -u
```

Now we can, for example, read the [ay_data_example.txt](#configuration-file) configuration file:

```
> get-data --datastore startup --filter-xpath=/ay-lense-example:*
DATA
<data xmlns="urn:ietf:params:xml:ns:yang:ietf-netconf-nmda">
  <ay-lense-example xmlns="aug:ay-lense-example">
    <config-file>/tmp/ay_data_example.txt</config-file>
    <variable-name-list>
      <_id>1</_id>
      <variable-name>
        <variable-name>b</variable-name>
        <number>3</number>
      </variable-name>
    </variable-name-list>
    <variable-name-list>
      <_id>2</_id>
      <variable-name>
        <variable-name>myverylongandshinyvariable</variable-name>
        <number>4</number>
      </variable-name>
    </variable-name-list>
  </ay-lense-example>
</data>
```

As is obvious, by choosing `--datastore` we defined that we are interested in a **startup** datastore.
Using the `--filter-xpath` option, we filtered the data that only applies to the `ay-lense-example` module.
By the way, we should get the same result if we use the `get-config` command (but the `--datastore` option must be changed to `--source`).

Now let's edit the configuration file:

```
> edit-data --datastore startup --config=./aydata.xml
OK
```

Using the `--config` option, we selected our prepared `aydata.xml` file with changes.
After using the command, the `/tmp/ay_data_example.txt` configuration file is modified:

```
remotely_changed <- 564
b <- 3
```

Success.

## Summary

If you are creating new modules, check that:
- *augeas module* is valid using [augparse](#augparse)
- *yang module* is valid using `yanglint` or use `-y` option for [augyang](#augyang)

Verify the configuration file:
- *augeas module* has the correct paths to find the configuration files 
- check with [augmatch](#augmatch) command

Test basic functionality with the sysrepo plugin:
- check with [ay_startup](#ay_startup) command

Use *yang module* in sysrepo:
- If the `srds_augeas.so` plugin is not installed, it must be installed by [sysrepoctl](#sysrepoctl)
- Install `augeas-extension.yang` and other *yang modules* using [sysrepoctl](#sysrepoctl)
