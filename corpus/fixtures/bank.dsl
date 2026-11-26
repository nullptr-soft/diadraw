workspace "Big Bank" "Internet banking" {
    !identifiers hierarchical

    model {
        customer = person "Personal Banking Customer" "A customer of the bank."
        group "Big Bank plc" {
            staff = person "Back Office Staff"
            mainframe = softwareSystem "Mainframe" "Core banking." "Existing System"
            ibs = softwareSystem "Internet Banking System" {
                spa = container "Single-Page App" "Banking UI" "React"
                api = container "API Application" "JSON API" "Java" {
                    signin = component "Sign In Controller" "Logs users in" "Spring MVC"
                    accounts = component "Accounts Controller" "" "Spring MVC"
                    signin -> accounts "redirects"
                }
                db = container "Database" "Users and logs" "Oracle" "Database"
            }
        }
        email = softwareSystem "E-mail System" "" "Existing System"

        customer -> ibs.spa "Views account balances"
        ibs.spa -> ibs.api "Makes API calls" "JSON/HTTPS"
        ibs.api -> ibs.db "Reads from and writes to" "JDBC"
        ibs.api -> mainframe "Uses" "XML/HTTPS"
        ibs.api -> email "Sends e-mail using"
        email -> customer "Sends e-mails to"
        staff -> mainframe "Uses"

        deploymentEnvironment "Live" {
            deploymentNode "Data center" {
            }
        }
    }

    views {
        systemLandscape landscape {
            include *
            autoLayout lr
        }
        systemContext ibs context {
            include *
            autoLayout
        }
        container ibs containers {
            include *
            autoLayout lr
        }
        component ibs.api components {
            include *
        }
        dynamic ibs signin {
            customer -> ibs.spa "Signs in"
        }
        styles {
            element "Database" {
                shape cylinder
                background #1168bd
            }
            element "Person" {
                shape person
            }
        }
        theme default
    }
}
