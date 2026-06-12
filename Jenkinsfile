#!groovy
import org.jenkinsci.plugins.workflow.steps.FlowInterruptedException

def get_os_distro() {
  return sh(label: 'Assign env.OS_DISTRO based on OS env', script: '''#!/bin/bash -le
    echo ${OS_DISTRO}''', returnStdout: true).trim()
}

def initializeEnvironment() {
  env.DRIVER_DISPLAY_NAME = 'Cassandra C/C++ Driver'
  env.DRIVER_TYPE = 'CASS'
  env.DRIVER_METRIC_TYPE = 'oss'
  env.DRIVER_BUILD_SCRIPT = '.build.sh'
  env.DRIVER_CASSANDRA_HEADER = 'include/cassandra.h'
  env.DRIVER_VERSION_HEADER = 'include/cassandra.h'
  env.DRIVER_LIBRARY = 'cassandra'
  if (env.GIT_URL.contains('cpp-driver-private')) {
    env.DRIVER_DISPLAY_NAME = 'private ' + env.DRIVER_DISPLAY_NAME
    env.DRIVER_METRIC_TYPE = 'oss-private'
  } else if (env.GIT_URL.contains('cpp-dse-driver')) {
    env.DRIVER_DISPLAY_NAME = 'DSE C/C++ Driver'
    env.DRIVER_TYPE = 'DSE'
    env.DRIVER_METRIC_TYPE = 'dse'
    env.DRIVER_BUILD_SCRIPT = "cpp-driver/${env.DRIVER_BUILD_SCRIPT}"
    env.DRIVER_CASSANDRA_HEADER = 'cpp-driver/include/cassandra.h'
    env.DRIVER_VERSION_HEADER = 'include/dse.h'
    env.DRIVER_LIBRARY = 'dse'
  }

  env.DRIVER_VERSION = sh(label: 'Determine driver version', script: '''#!/bin/bash -le
    . ${DRIVER_BUILD_SCRIPT}

    echo $(get_driver_version "${DRIVER_VERSION_HEADER}" "${DRIVER_TYPE}")''', returnStdout: true).trim()

  env.GIT_SHA = "${env.GIT_COMMIT.take(7)}"
  env.GITHUB_PROJECT_URL = "https://${GIT_URL.replaceFirst(/(git@|http:\/\/|https:\/\/)/, '').replace(':', '/').replace('.git', '')}"
  env.GITHUB_BRANCH_URL = "${GITHUB_PROJECT_URL}/tree/${env.BRANCH_NAME}"
  env.GITHUB_COMMIT_URL = "${GITHUB_PROJECT_URL}/commit/${env.GIT_COMMIT}"

  if (env.CI_INTEGRATION_ENABLED == 'true') {
    sh label: 'Download Apache Cassandra&reg; or DataStax Enterprise', script: '''#!/bin/bash -lex
      . ${CCM_ENVIRONMENT_SHELL} ${SERVER_VERSION}
    '''
  }

  if (env.SERVER_VERSION && env.SERVER_VERSION.split('-')[0] == 'dse') {
      env.DSE_FIXED_VERSION = env.SERVER_VERSION.split('-')[1]
      sh label: 'Update environment for DataStax Enterprise', script: '''#!/bin/bash -le
        cat >> ${HOME}/environment.txt << ENVIRONMENT_EOF
CCM_PATH=${HOME}/ccm
CCM_CASSANDRA_VERSION=${DSE_FIXED_VERSION} # maintain for backwards compatibility
CCM_VERSION=${DSE_FIXED_VERSION}
CCM_SERVER_TYPE=dse
DSE_VERSION=${DSE_FIXED_VERSION}
CCM_IS_DSE=true
CCM_BRANCH=${DSE_FIXED_VERSION}
DSE_BRANCH=${DSE_FIXED_VERSION}
ENVIRONMENT_EOF
      '''
  }

  sh label: 'Display C++ version and environment information', script: '''#!/bin/bash -le
    . ${DRIVER_BUILD_SCRIPT}

    # Load CCM environment variables
    if [ -f ${HOME}/environment.txt ]; then
      set -o allexport
      . ${HOME}/environment.txt
      set +o allexport
    fi

    if [ "${OS_DISTRO}" = "osx" ]; then
      clang --version
    else
      g++ --version
    fi
    cmake --version
    printenv | sort
  '''
}

def installDependencies() {
  sh label: 'Install dependencies', script: '''#!/bin/bash -le
    . ${DRIVER_BUILD_SCRIPT}

    install_dependencies
  '''
}

def buildDriver() {
  sh label: 'Build driver', script: '''#!/bin/bash -le
    . ${DRIVER_BUILD_SCRIPT}

    build_driver "${DRIVER_TYPE}"
    mkdir -p ${OS_DISTRO}/${OS_DISTRO_RELEASE}
    if [ -f build/cassandra-unit-tests ]; then
      cp build/cassandra-unit-tests ${OS_DISTRO}/${OS_DISTRO_RELEASE}
    fi
    if [ -f build/dse-unit-tests ]; then
      cp build/dse-unit-tests ${OS_DISTRO}/${OS_DISTRO_RELEASE}
    fi
    if [ -f build/cassandra-integration-tests ]; then
      cp build/cassandra-integration-tests ${OS_DISTRO}/${OS_DISTRO_RELEASE}
    fi
    if [ -f build/dse-integration-tests ]; then
      cp build/dse-integration-tests ${OS_DISTRO}/${OS_DISTRO_RELEASE}
    fi
    if [ -f build/CMakeFiles/CMakeOutput.log ]; then
      cp build/CMakeFiles/CMakeOutput.log ${OS_DISTRO}/${OS_DISTRO_RELEASE}
    fi
    if [ -f build//CMakeFiles/CMakeError.log ]; then
      cp build//CMakeFiles/CMakeError.log ${OS_DISTRO}/${OS_DISTRO_RELEASE}
    fi
  '''
}

def validateDriverExports() {
  sh label: 'Validate driver exports', script: '''#!/bin/bash -le
    . ${DRIVER_BUILD_SCRIPT}

    FUNCTIONS+=($(grep -Eoh '(^cass_\\s*(\\w+)\\s*\\()|(^dse_\\s*(\\w+)\\s*\\()' include/dse.h | awk -F '(' '{print $1}'));
    FUNCTIONS+=($(grep -Eoh '^cass_\\s*(\\w+)\\s*\\(' ${DRIVER_CASSANDRA_HEADER} | awk -F '(' '{print $1}'))
    check_driver_exports "${DRIVER_LIBRARY}" "${FUNCTIONS[@]}"
  '''
}

def configureTestingEnvironment() {
  sh label: 'Configure testing environment', script: '''#!/bin/bash -le
    . ${DRIVER_BUILD_SCRIPT}

    configure_testing_environment
  '''
}

def buildDocuments() {
  sh label: 'Building documents with documentor', script: '''#!/bin/bash -le
    documentor --output-directory ${HOME}/docs/drivers/cpp .
  '''

  sh label: 'Archive documentor generated documents', script: '''#!/bin/bash -le
    (
      cd ${HOME}/docs/drivers/cpp
      prefix=cpp-driver
      if [ "${DRIVER_TYPE}" = 'DSE' ]; then
        prefix=cpp-dse-driver
      fi
      tar czf ${WORKSPACE}/${prefix}-documents.tgz -C ${HOME}/docs/drivers/cpp .
    )
  '''
}

def executeUnitTests() {
  def attempt = 0
  retry(3) {
    try {
      timeout(time: 10, unit: 'MINUTES') {
        sh label: 'Execute unit tests', script: '''#!/bin/bash -le
          . ${DRIVER_BUILD_SCRIPT}

          build/cassandra-unit-tests --gtest_output=xml:cassandra-unit-tests-${OS_DISTRO}-${OS_DISTRO_RELEASE}-results.xml
          if [ -f build/dse-unit-tests ]; then
            build/dse-unit-tests --gtest_output=xml:dse-unit-tests-${OS_DISTRO}-${OS_DISTRO_RELEASE}-results.xml
          fi
        '''
      }
    } catch (FlowInterruptedException fie) {
      attempt++
      def message = "Retry attempt will be performed"
      if (attempt >= 3) {
        message = "No more retries will be performed"
      }
      // Work around https://issues.jenkins-ci.org/browse/JENKINS-51454
      error "Timeout Has Exceeded: ${message}"
    }
  }
}

def executeIntegrationTests() {
  timeout(time: 5, unit: 'HOURS') {
    sh label: 'Execute integration tests', script: '''#!/bin/bash -le
      . ${DRIVER_BUILD_SCRIPT}

      # Load CCM environment variables
      set -o allexport
      . ${HOME}/environment.txt
      set +o allexport

      # Disable exit on non 0
      set +e

      # Limit execution time on static nodes (no integration tests should be run on static node)
      if [ "${OS_VERSION}" != "osx/high-sierra" ]; then
        if [ -z "${TEST_FILTER}" ]; then
          TEST_FILTER=*
        fi
        # Limit DBAAS tests to Ubuntu only.  We used to limit this to just Ubuntu 18.04 but any current
        # Ubuntu runner should have the necessary deps.
        if [ "${OS_VERSION}" != "ubuntu"* ]; then
          # Handle filters with negative filtering already applied
          if [[ "${TEST_FILTER}" == *":-"* ]]; then
            TEST_FILTER=${TEST_FILTER}:DbaasTests.*
          else
            TEST_FILTER=${TEST_FILTER}:-DbaasTests.*
          fi
        fi

        # Copy any JAR files to the current working directory
        cp build/*.jar .

        build/cassandra-integration-tests --${CCM_SERVER_TYPE} \
                                          --category=cassandra \
                                          --gtest_filter=${TEST_FILTER} \
                                          --gtest_output=xml:cassandra-integration-tests-${OS_DISTRO}-${OS_DISTRO_RELEASE}-results.xml \
                                          --version=${CCM_VERSION} \
                                          --verbose || cassandra_error=true
        if [ "${CCM_IS_DSE}" = "true" ]; then
          build/${DRIVER_LIBRARY}-integration-tests --${CCM_SERVER_TYPE} \
                                                    --category=dse \
                                                    --gtest_filter=${TEST_FILTER} \
                                                    --gtest_output=xml:dse-integration-tests-${OS_DISTRO}-${OS_DISTRO_RELEASE}-results.xml \
                                                    --version=${CCM_VERSION} \
                                                    --verbose || dse_error=true
        fi

        # Archive logs if an error occurred and ensure exit status is returned
        if [ ${cassandra_error} ] || [ ${dse_error} ]; then
          if [ -d log ]; then
            mkdir -p ${OS_DISTRO}/${OS_DISTRO_RELEASE}/${CCM_SERVER_TYPE}/${CCM_VERSION}
            (
              cd log
              tar czf ../${OS_DISTRO}/${OS_DISTRO_RELEASE}/${CCM_SERVER_TYPE}/${CCM_VERSION}/${DRIVER_LIBRARY}-integration-tests-driver-logs.tgz *
            )
          fi

          exit 1
        fi
      fi
    '''
  }
}

def describePerCommitStage() {
  script {
    currentBuild.displayName = "Per-Commit build of ${env.BRANCH_NAME}"
    currentBuild.description = "Per-Commit build, export validation, unit testing, package building, and testing of installed library for branch ${env.BRANCH_NAME}"
  }
}

def describeScheduledAndAdhocTestingStage() {
  script {
    if (params.CI_SCHEDULE != 'DO-NOT-CHANGE-THIS-SELECTION') {
      currentBuild.displayName = "${params.CI_SCHEDULE.toLowerCase().capitalize()} schedule on ${env.OS_VERSION}"
    } else {
      currentBuild.displayName = "${params.ADHOC_BUILD_AND_EXECUTE_TESTS_SERVER_VERSION} on ${env.OS_VERSION}"
    }

    def serverVersions = env.SERVER_VERSIONS.split(' ')
    def serverDisplayNames = ''
    serverVersions.each {
      def serverType = it.split('-')[0]
      def serverDisplayName = 'Apache Cassandra&reg;'
      if (serverType == 'ALL') {
        serverDisplayName = "all Apache Cassandra&reg; and DataStax Enterprise server versions"
      } else {
        def serverVersion = it
        try {
          serverVersion = it.split('-')[1]
        } catch (e) {
          ;; // no-op
        }
        if (serverType == 'ddac') {
          serverDisplayName = "DataStax Distribution of ${serverDisplayName}"
        } else if (serverType == 'dse') {
          serverDisplayName = 'DataStax Enterprise'
        }
        serverDisplayNames += "${serverDisplayName} v${serverVersion}.x"
        if (it != serverVersions[-1]) {
          serverDisplayNames += ', '
          if (it != serverVersions[-2]) {
            serverDisplayNames += 'and '
          }
        }
      }
    }
    currentBuild.description = "Testing ${serverDisplayNames} on ${env.OS_VERSION}"
  }
}

pipeline {
  agent none

  // Global pipeline timeout
  options {
    timeout(time: 10, unit: 'HOURS')
    buildDiscarder(logRotator(artifactNumToKeepStr: '10', // Keep only the last 10 artifacts
                              numToKeepStr: '50'))        // Keep only the last 50 build records
  }

  parameters {
    choice(
      name: 'ADHOC_BUILD_TYPE',
      choices: ['BUILD', 'BUILD-AND-EXECUTE-TESTS'],
      description: '''Perform a adhoc build operation
                      <table style="width:100%">
                        <col width="25%">
                        <col width="75%">
                        <tr>
                          <th align="left">Choice</th>
                          <th align="left">Description</th>
                        </tr>
                        <tr>
                          <td><strong>BUILD</strong></td>
                          <td>Performs a <b>Per-Commit</b> build</td>
                        </tr>
                        <tr>
                          <td><strong>BUILD-AND-EXECUTE-TESTS</strong></td>
                          <td>
                            Performs a build and executes the integration and unit tests<br/>
                            <ul>
                              <li>Use <b>ADHOC_BUILD_AND_EXECUTE_TESTS_SERVER_VERSION</b> to change Apache Cassandra&reg; and DataStax Enterprise selection</li>
                              <li>Use <b>OS_VERSION</b> to change operating system selection</li>
                              <li>USE <b>INTEGRATION_TESTS_FILTER</b> to limit integration tests</li>
                            </ul>
                          </td>
                        </tr>
                      </table>
                      <br/>''')
    choice(
      name: 'ADHOC_BUILD_AND_EXECUTE_TESTS_SERVER_VERSION',
      choices: [
                '3.0',      // Previous Apache Cassandra
                '3.11',     // Current Apache Cassandra
                '4.0',      // Development Apache Cassandra
                'dse-5.1.35',  // Legacy DataStax Enterprise
                'dse-6.8.30',  // Development DataStax Enterprise
                'ALL'],
      description: '''Apache Cassandra&reg; and DataStax Enterprise server version to use for adhoc <b>BUILD-AND-EXECUTE-TESTS</b> builds
                      <table style="width:100%">
                        <col width="15%">
                        <col width="85%">
                        <tr>
                          <th align="left">Choice</th>
                          <th align="left">Description</th>
                        </tr>
                        <tr>
                          <td><strong>3.0</strong></td>
                          <td>Apache Cassandra&reg; v3.0.x</td>
                        </tr>
                        <tr>
                          <td><strong>3.11</strong></td>
                          <td>Apache Cassandra&reg; v3.11.x</td>
                        </tr>
                        <tr>
                          <td><strong>4.0</strong></td>
                          <td>Apache Cassandra&reg; v4.x (<b>CURRENTLY UNDER DEVELOPMENT</b>)</td>
                        </tr>
                        <tr>
                          <td><strong>dse-5.1</strong></td>
                          <td>DataStax Enterprise v5.1.x</td>
                        </tr>
                        <tr>
                          <td><strong>dse-6.8</strong></td>
                          <td>DataStax Enterprise v6.8.x (<b>CURRENTLY UNDER DEVELOPMENT</b>)</td>
                        </tr>
                      </table>''')
    choice(
      name: 'OS_VERSION',
      choices: ['rocky/9-64/cpp',
                'rocky/10-64/cpp',
                'ubuntu/jammy64/cpp',
                'ubuntu/noble64/cpp'],
      description: '''Operating system to use for scheduled or adhoc builds
                      <table style="width:100%">
                        <col width="20%">
                        <col width="80%">
                        <tr>
                          <th align="left">Choice</th>
                          <th align="left">Description</th>
                        </tr>
                        <tr>
                          <td><strong>rocky/9-64/cpp</strong></td>
                          <td>Rocky Linux 9 x86_64</td>
                        </tr>
                        <tr>
                          <td><strong>rocky/10-64/cpp</strong></td>
                          <td>Rocky Linux 10 x86_64</td>
                        </tr>
                        <tr>
                          <td><strong>ubuntu/jammy64/cpp</strong></td>
                          <td>Ubuntu 22.04 LTS x86_64</td>
                        </tr>
                        <tr>
                          <td><strong>ubuntu/noble64/cpp</strong></td>
                          <td>Ubuntu 24.04 LTS x86_64</td>
                        </tr>
                      </table>''')
    string(
      name: 'INTEGRATION_TESTS_FILTER',
      defaultValue: '',
      description: '''<p><b>POSITIVE_PATTERNS[-NEGATIVE_PATTERNS]</b></p>
                      Run only the tests whose name matches one of the positive patterns but none of the negative patterns. <b>\'?\'</b> matches any single character; <b>\'*\'</b> matches any substring; <b>\':\'</b> separates two patterns.''')
    string(
      name: 'LIBUV_VERSION',
      defaultValue: '1.52.1',
      description: '''<p>libuv version to build and use</p>
                      <b>Note:</b> Rarely does this need to change.''')
    choice(
      name: 'CI_SCHEDULE',
      choices: ['DO-NOT-CHANGE-THIS-SELECTION', 'WEEKNIGHTS', 'WEEKENDS', 'MONTHLY'],
      description: 'CI testing schedule to execute periodically scheduled builds and tests of the driver (<strong>DO NOT CHANGE THIS SELECTION</strong>)')
    string(
      name: 'CI_SCHEDULE_SERVER_VERSIONS',
      defaultValue: 'DO-NOT-CHANGE-THIS-SELECTION',
      description: 'CI testing server version(s) to utilize for scheduled test runs of the driver (<strong>DO NOT CHANGE THIS SELECTION</strong>)')
  }

  environment {
    LIBUV_VERSION = "${params.LIBUV_VERSION}"
    CCM_ENVIRONMENT_SHELL = '/usr/local/bin/ccm_environment.sh'
  }

  stages {
    stage('Per-Commit') {
      options {
        timeout(time: 1, unit: 'HOURS')
      }
      when {
        beforeAgent true
        allOf {
          expression { params.ADHOC_BUILD_TYPE == 'BUILD' }
          expression { params.CI_SCHEDULE == 'DO-NOT-CHANGE-THIS-SELECTION' }
          expression { params.CI_SCHEDULE_SERVER_VERSIONS == 'DO-NOT-CHANGE-THIS-SELECTION' }
        }
      }

      matrix {
        axes {
          axis {
            name 'OS_VERSION'
            values 'rocky/9-64/cpp',
                   'rocky/10-64/cpp',
                   'ubuntu/jammy64/cpp',
                   'ubuntu/noble64/cpp'
          }
        }

        agent {
          label "${env.OS_VERSION}"
        }

        stages {
          stage('Initialize-Environment') {
            steps {
              initializeEnvironment()
            }
          }
          stage('Describe-Build') {
            steps {
              describePerCommitStage()
            }
          }
          stage('Install-Dependencies') {
            steps {
              installDependencies()
            }
            post {
              success {
                // Allow empty results for 'osx/high-sierra' which doesn't produce packages
                script {
                  def distro = get_os_distro()
                  archiveArtifacts artifacts: "${distro}/**/libuv*", allowEmptyArchive: true
                }
              }
            }
          }
          stage('Build-Driver') {
            steps {
              buildDriver()
            }
            post {
              success {
                script {
                  def distro = get_os_distro()
                  archiveArtifacts artifacts: "${distro}/**/cassandra-*-tests"
                  archiveArtifacts artifacts: "${distro}/**/dse-*-tests", allowEmptyArchive: true
                }
              }
              failure {
                script {
                  def distro = get_os_distro()
                  archiveArtifacts artifacts: "${distro}/**/CMakeOutput.log"
                  archiveArtifacts artifacts: "${distro}/**/CMakeError.log"
                }
              }
            }
          }
          stage('Validate-Driver-Exports') {
            steps {
              validateDriverExports()
            }
          }
          stage('Execute-Unit-Tests') {
            steps {
              configureTestingEnvironment()
              executeUnitTests()
            }
            post {
              always {
                // Allow empty results if segfault occurs
                junit testResults: '*unit-tests-*-results.xml', allowEmptyResults: true
              }
            }
          }
        }
        post {
          cleanup {
            cleanWs()
          }
        }
      }
    }

    stage('Scheduled-And-Adhoc-Testing') {
      when {
        beforeAgent true
        allOf {
          not { buildingTag() }
          anyOf {
            expression { params.ADHOC_BUILD_TYPE == 'BUILD-AND-EXECUTE-TESTS' }
            allOf {
              expression { params.ADHOC_BUILD_TYPE == 'BUILD' }
              expression { params.CI_SCHEDULE != 'DO-NOT-CHANGE-THIS-SELECTION' }
              expression { params.CI_SCHEDULE_SERVER_VERSIONS != 'DO-NOT-CHANGE-THIS-SELECTION' }
            }
          }
        }
      }

      environment {
        CI_INTEGRATION_ENABLED = 'true'
        TEST_FILTER = "${params.INTEGRATION_TESTS_FILTER}"
        OS_VERSION = "${params.OS_VERSION}"
        SERVER_VERSIONS = "${params.CI_SCHEDULE_SERVER_VERSIONS == 'DO-NOT-CHANGE-THIS-SELECTION' ? params.ADHOC_BUILD_AND_EXECUTE_TESTS_SERVER_VERSION : params.CI_SCHEDULE_SERVER_VERSIONS}"
      }

      matrix {
        axes {
          axis {
            name 'SERVER_VERSION'
            values '3.0',      // Previous Apache Cassandra
                   '3.11',     // Current Apache Cassandra
                   '4.0',      // Development Apache Cassandra
                   'dse-5.1.35',  // Legacy DataStax Enterprise
                   'dse-6.8.30'   // Development DataStax Enterprise
          }
        }
        when {
          beforeAgent true
          allOf {
            expression { return env.SERVER_VERSIONS.split(' ').any { it =~ /(ALL|${env.SERVER_VERSION})/ } }
          }
        }

        agent {
          label "${params.OS_VERSION}"
        }

        stages {
          stage('Initialize-Environment') {
            steps {
              initializeEnvironment()
            }
          }
          stage('Describe-Build') {
            steps {
              describeScheduledAndAdhocTestingStage()
            }
          }
          stage('Install-Dependencies') {
            steps {
              installDependencies()
            }
          }
          stage('Build-Driver') {
            steps {
              buildDriver()
            }
            post {
              failure {
                script {
                  def distro = get_os_distro()
                  archiveArtifacts artifacts: "${distro}/**/CMakeOutput.log"
                  archiveArtifacts artifacts: "${distro}/**/CMakeError.log"
                }
              }
            }
          }
          stage('Execute-Integration-Tests') {
            steps {
              configureTestingEnvironment()
              executeIntegrationTests()
            }
            post {
              always {
                // Allow empty results for when segfaults occur
                junit testResults: '*integration-tests-*-results.xml', allowEmptyResults: true
              }
              failure {
                script {
                  def distro = get_os_distro()
                  archiveArtifacts artifacts: "${distro}/**/*-integration-tests-driver-logs.tgz"
                }
              }
              cleanup {
                cleanWs()
              }
            }
          }
        }
      }
    }
  }
}
