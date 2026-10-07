@echo off

set RULE_NAME=Syzygy

rem Delete the rule
netsh advfirewall firewall delete rule name=%RULE_NAME%
