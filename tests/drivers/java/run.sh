#!/bin/bash

JAVA=java
JAVAC=javac

if ! which $JAVA >/dev/null; then
    echo "Please install java!"
    exit 1
fi

if ! which $JAVAC >/dev/null; then
    echo "Please install javac!"
    exit 1
fi

DRIVER=neo4j-java-driver.jar

if [ ! -f $DRIVER ]; then
    wget -O $DRIVER http://central.maven.org/maven2/org/neo4j/driver/neo4j-java-driver/1.3.1/neo4j-java-driver-1.3.1.jar || exit 1
fi

javac -classpath .:$DRIVER test.java || exit 1
java -classpath .:$DRIVER test || exit 1
