from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node
from launch_ros.parameter_descriptions import ParameterValue
import os


def generate_launch_description():
    share = get_package_share_directory('s100_bringup')
    return LaunchDescription([
        DeclareLaunchArgument('max_cycles', default_value='0'),
        Node(
            package='s100_robot_hardware',
            executable='s100_hardware_node',
            namespace='s100',
            output='screen',
            parameters=[os.path.join(share, 'config', 'offline.yaml'), {
                # This entry always stays offline, even if the YAML is edited.
                'backend': 'mock',
                'max_cycles': ParameterValue(LaunchConfiguration('max_cycles'), value_type=int),
            }],
        ),
    ])
